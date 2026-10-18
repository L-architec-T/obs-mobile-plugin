#pragma once

#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>

class QVBoxLayout;
class QLabel;

namespace obsmobile {

class ControlServer;
class DeviceStore;
class PhoneConnection;

/**
 * The dockable "Téléphones" panel: visibility on the network, pending connection requests,
 * connected phones, remembered phones (with "Oublier") and "Tout déconnecter".
 */
class PhonesDock : public QWidget {
	Q_OBJECT
public:
	PhonesDock(ControlServer &server, DeviceStore &store, std::function<bool()> visibleOnNetwork,
		   QWidget *parent = nullptr);

	void showPairing(PhoneConnection *phone);

private:
	void rebuild();
	void clear(QVBoxLayout *layout);

	ControlServer &server;
	DeviceStore &store;
	std::function<bool()> visibleOnNetwork;
	QLabel *networkStatus;
	QVBoxLayout *requests;
	QVBoxLayout *connected;
	QVBoxLayout *remembered;
	QTimer refreshTimer;
};

} // namespace obsmobile
