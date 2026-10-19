#include "control-server.hpp"

#include "protocol.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QHostInfo>
#include <QJsonDocument>

namespace obsmobile {

PhoneConnection::PhoneConnection(QTcpSocket *s, DeviceStore &deviceStore, ProgramReturn &programReturn,
				 std::function<int()> ports, QObject *parent)
	: QObject(parent),
	  socket(s),
	  allocatePort(std::move(ports)),
	  store(deviceStore),
	  programReturn(programReturn)
{
	connect(&stateTimer, &QTimer::timeout, this, &PhoneConnection::pushState);
	connect(&pingTimer, &QTimer::timeout, this, &PhoneConnection::ping);
	socket->setParent(this);
	phone.address = socket->peerAddress().toString().remove(QStringLiteral("::ffff:"));
	connect(socket, &QTcpSocket::readyRead, this, &PhoneConnection::onReadyRead);
	connect(socket, &QTcpSocket::disconnected, this, &PhoneConnection::closed);

	send({{"type", "welcome"},
	      {"protocol", kProtocolVersion},
	      {"pc", QHostInfo::localHostName()},
	      {"obs", QString::fromUtf8(obs_get_version_string())}});
}

PhoneConnection::~PhoneConnection()
{
	programReturn.release(this);
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
	else if (pair == PairState::Accepted)
		handleAccepted(type, message);
	// Anything else from a phone that is not accepted is ignored on purpose.
}

void PhoneConnection::onAccepted()
{
	camera = std::make_unique<PhoneCamera>(phone.name, allocatePort());
	stateTimer.start(500);
	pingTimer.start(3000);
	ping();
}

void PhoneConnection::handleAccepted(const QString &type, const QJsonObject &message)
{
	if (type == "camera") {
		if (message["state"].toString() == "start") {
			camera->start(layoutFromName(message["layout"].toString()));
			send({{"type", "stream"}, {"port", camera->port()}, {"latencyMs", 80}});
		} else {
			camera->stop();
		}
		pushState();
	} else if (type == "layout") {
		camera->setLayout(layoutFromName(message["layout"].toString()));
		pushState();
	} else if (type == "addToScene") {
		camera->addToCurrentScene();
		pushState();
	} else if (type == "pong") {
		if (message["id"].toInt(-1) == pingId && pingClock.isValid()) {
			phone.rttMs = int(pingClock.elapsed());
			pingClock.invalidate();
			emit changed();
		}
	} else if (type == "return") {
		if (message["state"].toString() == "start") {
			const QSize size = programReturn.size();
			send({{"type", "return"},
			      {"port", kReturnPort},
			      {"key", QString::fromLatin1(programReturn.issueKey(this))},
			      {"width", size.width()},
			      {"height", size.height()}});
		} else {
			programReturn.release(this);
		}
	}
}

void PhoneConnection::ping()
{
	// An unanswered ping means a phone that stopped answering: no figure rather than a stale one.
	if (pingClock.isValid() && pingClock.elapsed() > 3000)
		phone.rttMs = -1;
	pingClock.start();
	send({{"type", "ping"}, {"id", ++pingId}});
}

void PhoneConnection::pushState()
{
	if (!camera)
		return;
	QJsonObject state = camera->tick();
	if (const bool onAir = state["onAir"].toBool(); onAir != phone.onAir) {
		phone.onAir = onAir;
		emit changed();
	}
	obs_source_t *current = obs_frontend_get_current_scene();
	state["scene"] = current ? QString::fromUtf8(obs_source_get_name(current)) : QString();
	obs_source_release(current);
	const bool live = obs_frontend_streaming_active();
	state["live"] = live;
	if (live) {
		obs_output_t *output = obs_frontend_get_streaming_output();
		obs_video_info ovi = {};
		obs_get_video_info(&ovi);
		if (output && ovi.fps_num)
			state["durationMs"] = double(obs_output_get_total_frames(output)) * 1000.0 * ovi.fps_den / ovi.fps_num;
		obs_output_release(output);
	}
	// On change, and every 2 s anyway so the phone's stream timer stays in sync.
	if (state != lastState || ++ticksSinceSent >= 4) {
		lastState = state;
		ticksSinceSent = 0;
		state["type"] = "state";
		send(state);
	}
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
		onAccepted();
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
	onAccepted();
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

ControlServer::ControlServer(DeviceStore &deviceStore, ProgramReturn &ret, QObject *parent)
	: QObject(parent),
	  store(deviceStore),
	  programReturn(ret)
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
		auto *phone = new PhoneConnection(socket, store, programReturn, [this] { return freePort(); }, this);
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

int ControlServer::freePort() const
{
	// One UDP port per phone: 9711, 9712… (9710 stays with the WebSocket "secours" mode).
	for (int port = kFirstCameraPort;; ++port) {
		bool used = false;
		for (auto *c : connections)
			used = used || c->cameraPort() == port;
		if (!used)
			return port;
	}
}

void ControlServer::disconnectAll()
{
	for (auto *c : connections)
		c->disconnectPhone();
}

} // namespace obsmobile
