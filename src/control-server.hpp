#pragma once

#include "device-store.hpp"
#include "phone-camera.hpp"
#include "program-return.hpp"

#include <QTimer>
#include <functional>
#include <memory>

#include <QHostAddress>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>

namespace obsmobile {

enum class PairState { WaitingHello, Pending, Accepted, Refused };

/** What a phone says about itself (hello + status messages). */
struct PhoneInfo {
	QString deviceId;
	QString name;
	QString model;
	QString appVersion;
	QString address;
	int battery = -1;
	bool charging = false;
	int wifiDbm = 0;
	double micDb = -100.0;
	/** Last camera picture sent while waiting for approval ("Aperçu · pas à l'antenne"). */
	QImage preview;
};

/** One phone's control connection. Lives on the UI thread. */
class PhoneConnection : public QObject {
	Q_OBJECT
public:
	/** [allocatePort] gives the UDP port this phone's camera stream will use. */
	PhoneConnection(QTcpSocket *socket, DeviceStore &store, ProgramReturn &programReturn,
			std::function<int()> allocatePort, QObject *parent);
	~PhoneConnection() override;

	/** SRT port of this phone's camera, 0 before it is accepted. */
	int cameraPort() const { return camera ? camera->port() : 0; }

	const PhoneInfo &info() const { return phone; }
	PairState state() const { return pair; }

	void accept(bool remember);
	void refuse();
	void disconnectPhone();

signals:
	void changed();
	/** Emitted once the hello is in and the PC must decide. */
	void approvalRequested();
	void closed();

private:
	void onReadyRead();
	void handle(const QJsonObject &message);
	void onHello(const QJsonObject &message);
	void send(const QJsonObject &message);
	void onAccepted();
	void handleAccepted(const QString &type, const QJsonObject &message);
	void pushState();

	QPointer<QTcpSocket> socket;
	std::function<int()> allocatePort;
	std::unique_ptr<PhoneCamera> camera;
	QTimer stateTimer;
	QJsonObject lastState;
	int ticksSinceSent = 0;
	DeviceStore &store;
	ProgramReturn &programReturn;
	PhoneInfo phone;
	PairState pair = PairState::WaitingHello;
	QByteArray buffer;
};

/**
 * Listens for phones on [kControlPort]. Unapproved phones can only introduce themselves and send
 * a preview; nothing they say reaches OBS until the PC accepts them.
 */
class ControlServer : public QObject {
	Q_OBJECT
public:
	ControlServer(DeviceStore &store, ProgramReturn &programReturn, QObject *parent = nullptr);

	bool start();
	QList<PhoneConnection *> phones() const { return connections; }
	void disconnectAll();

signals:
	void phonesChanged();
	void approvalRequested(PhoneConnection *phone);

private:
	void onNewConnection();
	int pendingCount() const;
	int freePort() const;

	DeviceStore &store;
	ProgramReturn &programReturn;
	QTcpServer server;
	QList<PhoneConnection *> connections;
};

} // namespace obsmobile
