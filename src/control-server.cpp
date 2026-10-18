#include "control-server.hpp"

#include "protocol.hpp"

#include <obs.h>
#include <plugin-support.h>

#include <QHostInfo>
#include <QJsonDocument>

namespace obsmobile {

PhoneConnection::PhoneConnection(QTcpSocket *s, DeviceStore &deviceStore, QObject *parent)
	: QObject(parent),
	  socket(s),
	  store(deviceStore)
{
	socket->setParent(this);
	phone.address = socket->peerAddress().toString().remove(QStringLiteral("::ffff:"));
	connect(socket, &QTcpSocket::readyRead, this, &PhoneConnection::onReadyRead);
	connect(socket, &QTcpSocket::disconnected, this, &PhoneConnection::closed);

	send({{"type", "welcome"},
	      {"protocol", kProtocolVersion},
	      {"pc", QHostInfo::localHostName()},
	      {"obs", QString::fromUtf8(obs_get_version_string())}});
}

void PhoneConnection::send(const QJsonObject &message)
{
	if (!socket)
		return;
	socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact));
	socket->write("\n");
}

void PhoneConnection::onReadyRead()
{
	buffer.append(socket->readAll());
	qsizetype newline;
	while ((newline = buffer.indexOf('\n')) >= 0) {
		const QByteArray line = buffer.left(newline);
		buffer.remove(0, newline + 1);
		const auto doc = QJsonDocument::fromJson(line);
		if (doc.isObject())
			handle(doc.object());
	}
	if (buffer.size() > kMaxLineBytes) {
		obs_log(LOG_WARNING, "Phone %s sent an oversized message, disconnecting", qPrintable(phone.address));
		disconnectPhone();
	}
}

void PhoneConnection::handle(const QJsonObject &message)
{
	const QString type = message["type"].toString();
	if (type == "hello" && pair == PairState::WaitingHello) {
		onHello(message);
	} else if (type == "status") {
		phone.battery = message["battery"].toInt(phone.battery);
		phone.charging = message["charging"].toBool(phone.charging);
		phone.wifiDbm = message["wifi"].toInt(phone.wifiDbm);
		phone.micDb = message["micDb"].toDouble(phone.micDb);
		emit changed();
	} else if (type == "preview" && pair == PairState::Pending) {
		// Only while waiting for approval: once accepted the real stream takes over.
		QImage image;
		if (image.loadFromData(QByteArray::fromBase64(message["jpeg"].toString().toLatin1()), "JPG")) {
			phone.preview = image;
			emit changed();
		}
	}
	// Anything else from a phone that is not accepted is ignored on purpose.
}

void PhoneConnection::onHello(const QJsonObject &message)
{
	phone.deviceId = message["deviceId"].toString().left(64);
	phone.name = message["name"].toString().left(64);
	phone.model = message["model"].toString().left(64);
	phone.appVersion = message["app"].toString().left(16);
	if (phone.deviceId.isEmpty()) {
		disconnectPhone();
		return;
	}
	if (store.isTrusted(phone.deviceId, message["token"].toString())) {
		store.touch(phone.deviceId);
		pair = PairState::Accepted;
		send({{"type", "pairing"}, {"state", "accepted"}});
		obs_log(LOG_INFO, "Remembered phone %s (%s) reconnected", qPrintable(phone.name), qPrintable(phone.address));
		emit changed();
		return;
	}
	pair = PairState::Pending;
	send({{"type", "pairing"}, {"state", "pending"}});
	obs_log(LOG_INFO, "Phone %s (%s) asks to connect", qPrintable(phone.name), qPrintable(phone.address));
	emit changed();
	emit approvalRequested();
}

void PhoneConnection::accept(bool remember)
{
	if (pair != PairState::Pending)
		return;
	pair = PairState::Accepted;
	phone.preview = QImage();
	QJsonObject reply{{"type", "pairing"}, {"state", "accepted"}};
	if (remember)
		reply["token"] = store.remember(phone.deviceId, phone.name, phone.model);
	send(reply);
	obs_log(LOG_INFO, "Phone %s accepted%s", qPrintable(phone.name), remember ? " and remembered" : "");
	emit changed();
}

void PhoneConnection::refuse()
{
	if (pair != PairState::Pending)
		return;
	pair = PairState::Refused;
	send({{"type", "pairing"}, {"state", "refused"}});
	obs_log(LOG_INFO, "Phone %s refused", qPrintable(phone.name));
	if (socket)
		socket->disconnectFromHost();
}

void PhoneConnection::disconnectPhone()
{
	if (socket)
		socket->abort();
}

ControlServer::ControlServer(DeviceStore &deviceStore, QObject *parent) : QObject(parent), store(deviceStore)
{
	connect(&server, &QTcpServer::newConnection, this, &ControlServer::onNewConnection);
}

bool ControlServer::start()
{
	if (!server.listen(QHostAddress::Any, kControlPort)) {
		obs_log(LOG_ERROR, "Cannot listen on port %d: %s", kControlPort, qPrintable(server.errorString()));
		return false;
	}
	obs_log(LOG_INFO, "Listening for phones on port %d", kControlPort);
	return true;
}

int ControlServer::pendingCount() const
{
	int count = 0;
	for (auto *c : connections)
		if (c->state() == PairState::Pending || c->state() == PairState::WaitingHello)
			++count;
	return count;
}

void ControlServer::onNewConnection()
{
	while (QTcpSocket *socket = server.nextPendingConnection()) {
		if (pendingCount() >= kMaxPendingPhones) {
			obs_log(LOG_WARNING, "Too many pending phones, refusing %s", qPrintable(socket->peerAddress().toString()));
			socket->abort();
			socket->deleteLater();
			continue;
		}
		auto *phone = new PhoneConnection(socket, store, this);
		connections.append(phone);
		connect(phone, &PhoneConnection::changed, this, &ControlServer::phonesChanged);
		connect(phone, &PhoneConnection::approvalRequested, this, [this, phone] { emit approvalRequested(phone); });
		connect(phone, &PhoneConnection::closed, this, [this, phone] {
			connections.removeAll(phone);
			phone->deleteLater();
			emit phonesChanged();
		});
		emit phonesChanged();
	}
}

void ControlServer::disconnectAll()
{
	for (auto *c : connections)
		c->disconnectPhone();
}

} // namespace obsmobile
