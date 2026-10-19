#include "phones-dock.hpp"

#include "phone-camera.hpp"

#include "control-server.hpp"
#include "device-store.hpp"
#include "pairing-dialog.hpp"
#include "protocol.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace obsmobile {

namespace {

QLabel *sectionTitle(const QString &text)
{
	auto *label = new QLabel(text.toUpper());
	label->setStyleSheet(QStringLiteral("color:#A8AEB6;font-weight:600;letter-spacing:1px;margin-top:8px;"));
	return label;
}

/** RSSI in words: what a streamer needs to know is whether the Wi-Fi will hold. */
QString wifiQuality(int dbm)
{
	if (dbm >= -60)
		return QStringLiteral("excellent");
	if (dbm >= -70)
		return QStringLiteral("bon");
	if (dbm >= -80)
		return QStringLiteral("faible");
	return QStringLiteral("très faible");
}

QLabel *muted(const QString &text)
{
	auto *label = new QLabel(text);
	label->setStyleSheet(QStringLiteral("color:#7C838D;"));
	label->setWordWrap(true);
	return label;
}

} // namespace

PhonesDock::PhonesDock(ControlServer &s, DeviceStore &d, std::function<bool()> visible, QWidget *parent)
	: QWidget(parent),
	  server(s),
	  store(d),
	  visibleOnNetwork(std::move(visible))
{
	auto *content = new QWidget;
	auto *column = new QVBoxLayout(content);
	column->setContentsMargins(8, 8, 8, 8);

	networkStatus = new QLabel;
	networkStatus->setWordWrap(true);
	column->addWidget(networkStatus);

	requests = new QVBoxLayout;
	column->addLayout(requests);

	column->addWidget(sectionTitle(QStringLiteral("Connectés")));
	connected = new QVBoxLayout;
	column->addLayout(connected);

	column->addWidget(sectionTitle(QStringLiteral("Appareils mémorisés")));
	remembered = new QVBoxLayout;
	column->addLayout(remembered);

	column->addStretch();
	auto *disconnectAll = new QPushButton(QStringLiteral("Tout déconnecter"));
	connect(disconnectAll, &QPushButton::clicked, this, [this] { server.disconnectAll(); });
	column->addWidget(disconnectAll);
	auto *removeAll = new QPushButton(QStringLiteral("Retirer d'OBS ce qu'OBS Mobile a créé…"));
	connect(removeAll, &QPushButton::clicked, this, [this] {
		const auto answer = QMessageBox::question(
			this, QStringLiteral("Retirer OBS Mobile de la collection de scènes"),
			QStringLiteral("Les téléphones seront déconnectés, puis la scène « %1 » (y compris là où tu l'as "
				       "ajoutée) et les sources des téléphones seront supprimées. Tes autres scènes et sources "
				       "ne sont pas touchées.\n\nContinuer ?")
				.arg(QString::fromUtf8(kCamerasScene)));
		if (answer != QMessageBox::Yes)
			return;
		server.disconnectAll();
		// After the phones' connections are gone: they hide their sources on the way out.
		QTimer::singleShot(500, this, [this] {
			const int removed = removeEverythingCreated();
			QMessageBox::information(this, QStringLiteral("OBS Mobile"),
						 removed ? QStringLiteral("%1 élément(s) retiré(s) d'OBS.").arg(removed)
							 : QStringLiteral("Rien à retirer."));
		});
	});
	column->addWidget(removeAll);

	auto *scroll = new QScrollArea;
	scroll->setWidget(content);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->addWidget(scroll);

	connect(&server, &ControlServer::phonesChanged, this, &PhonesDock::rebuild);
	// mDNS registration completes asynchronously: refresh the status line now and then.
	const auto updateStatus = [this] {
		const bool visibleNow = visibleOnNetwork();
		networkStatus->setText(visibleNow ? QStringLiteral("● Visible sur le réseau (port %1)").arg(kControlPort)
						  : QStringLiteral("○ Pas encore visible sur le réseau (port %1)").arg(kControlPort));
		networkStatus->setStyleSheet(visibleNow ? QStringLiteral("color:#3DD68C;") : QStringLiteral("color:#F0B35A;"));
	};
	connect(&refreshTimer, &QTimer::timeout, this, updateStatus);
	refreshTimer.start(2000);
	updateStatus();
	rebuild();
}

void PhonesDock::clear(QVBoxLayout *layout)
{
	while (QLayoutItem *item = layout->takeAt(0)) {
		if (QWidget *w = item->widget())
			w->deleteLater();
		delete item;
	}
}

void PhonesDock::showPairing(PhoneConnection *phone)
{
	auto *dialog = new PairingDialog(phone, window());
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void PhonesDock::rebuild()
{
	clear(requests);
	clear(connected);
	clear(remembered);

	int connectedCount = 0;
	for (PhoneConnection *phone : server.phones()) {
		const PhoneInfo &info = phone->info();
		if (phone->state() == PairState::Pending) {
			auto *banner = new QFrame;
			banner->setStyleSheet(QStringLiteral("QFrame{background:#2B1A12;border:1px solid #FF6B2C;border-radius:8px;}"
							     "QLabel{border:none;background:transparent;}"));
			auto *row = new QHBoxLayout(banner);
			row->addWidget(new QLabel(QStringLiteral("%1 veut se connecter").arg(info.name)), 1);
			auto *open = new QPushButton(QStringLiteral("Voir"));
			connect(open, &QPushButton::clicked, this, [this, phone] { showPairing(phone); });
			row->addWidget(open);
			requests->addWidget(banner);
		} else if (phone->state() == PairState::Accepted) {
			++connectedCount;
			QStringList parts{info.name};
			if (info.onAir)
				parts << QStringLiteral("À L'ANTENNE");
			if (info.battery >= 0)
				parts << QStringLiteral("batterie %1 %%2").arg(info.battery).arg(info.charging ? QStringLiteral(" (en charge)") : QString());
			if (info.wifiDbm < 0)
				parts << QStringLiteral("Wi-Fi %1").arg(wifiQuality(info.wifiDbm));
			if (info.rttMs >= 0)
				parts << QStringLiteral("%1 ms").arg(info.rttMs);
			auto *row = new QWidget;
			auto *layout = new QHBoxLayout(row);
			layout->setContentsMargins(0, 0, 0, 0);
			layout->addWidget(new QLabel(parts.join(QStringLiteral(" · "))), 1);
			auto *drop = new QPushButton(QStringLiteral("Déconnecter"));
			connect(drop, &QPushButton::clicked, this, [phone] { phone->disconnectPhone(); });
			layout->addWidget(drop);
			connected->addWidget(row);
		}
	}
	if (connectedCount == 0)
		connected->addWidget(muted(QStringLiteral("Aucun téléphone. Ouvre OBS Mobile sur le même Wi-Fi.")));

	const auto devices = store.devices();
	for (const RememberedDevice &device : devices) {
		auto *row = new QWidget;
		auto *layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(new QLabel(QStringLiteral("%1 · %2").arg(device.name, device.model)), 1);
		auto *forget = new QPushButton(QStringLiteral("Oublier"));
		const QString id = device.deviceId;
		connect(forget, &QPushButton::clicked, this, [this, id] {
			store.forget(id);
			rebuild();
		});
		layout->addWidget(forget);
		remembered->addWidget(row);
	}
	if (devices.isEmpty())
		remembered->addWidget(muted(QStringLiteral("Aucun.")));
}

} // namespace obsmobile
