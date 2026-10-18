#include "pairing-dialog.hpp"

#include "control-server.hpp"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace obsmobile {

namespace {
constexpr int kPreviewWidth = 180;
constexpr int kPreviewHeight = 320; // 9:16, like the phone's stream
} // namespace

PairingDialog::PairingDialog(PhoneConnection *p, QWidget *parent) : QDialog(parent), phone(p)
{
	setWindowTitle(QStringLiteral("Demande de connexion · OBS Mobile"));
	setAttribute(Qt::WA_DeleteOnClose);
	setModal(false);

	preview = new QLabel;
	preview->setFixedSize(kPreviewWidth, kPreviewHeight);
	preview->setAlignment(Qt::AlignCenter);
	preview->setStyleSheet(QStringLiteral("background:#1A1D23;color:#7C838D;border-radius:8px;"));
	auto *previewCaption = new QLabel(QStringLiteral("Aperçu · pas à l'antenne"));
	previewCaption->setAlignment(Qt::AlignCenter);

	auto *previewColumn = new QVBoxLayout;
	previewColumn->addWidget(preview);
	previewColumn->addWidget(previewCaption);

	title = new QLabel;
	QFont titleFont = title->font();
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
	titleFont.setBold(true);
	title->setFont(titleFont);
	details = new QLabel;
	details->setTextFormat(Qt::PlainText);

	micLevel = new QProgressBar;
	micLevel->setRange(-60, 0);
	micLevel->setTextVisible(false);
	micLevel->setFixedHeight(10);

	remember = new QCheckBox(QStringLiteral("Se souvenir de cet appareil"));
	remember->setChecked(true);

	auto *refuseButton = new QPushButton(QStringLiteral("Refuser"));
	auto *acceptButton = new QPushButton(QStringLiteral("Accepter"));
	acceptButton->setDefault(true);
	connect(refuseButton, &QPushButton::clicked, this, [this] {
		if (phone)
			phone->refuse();
		close();
	});
	connect(acceptButton, &QPushButton::clicked, this, [this] {
		if (phone)
			phone->accept(remember->isChecked());
		close();
	});

	auto *buttons = new QHBoxLayout;
	buttons->addStretch();
	buttons->addWidget(refuseButton);
	buttons->addWidget(acceptButton);

	auto *infoColumn = new QVBoxLayout;
	infoColumn->addWidget(title);
	infoColumn->addWidget(details);
	infoColumn->addSpacing(8);
	infoColumn->addWidget(new QLabel(QStringLiteral("Micro du téléphone")));
	infoColumn->addWidget(micLevel);
	infoColumn->addStretch();
	infoColumn->addWidget(remember);
	infoColumn->addLayout(buttons);

	auto *layout = new QHBoxLayout(this);
	layout->addLayout(previewColumn);
	layout->addSpacing(16);
	layout->addLayout(infoColumn, 1);

	connect(phone, &PhoneConnection::changed, this, &PairingDialog::refresh);
	// The phone left or was decided elsewhere (another dialog, "Tout déconnecter"…).
	connect(phone, &QObject::destroyed, this, &QDialog::close);
	refresh();
}

void PairingDialog::refresh()
{
	if (!phone || phone->state() != PairState::Pending) {
		close();
		return;
	}
	const PhoneInfo &info = phone->info();
	title->setText(info.name);

	QStringList lines;
	lines << QStringLiteral("%1 · %2").arg(info.model, info.address);
	QStringList status;
	if (info.battery >= 0)
		status << QStringLiteral("Batterie %1 %%2").arg(info.battery).arg(info.charging ? " · en charge" : "");
	if (info.wifiDbm < 0)
		status << QStringLiteral("Wi-Fi %1 dBm").arg(info.wifiDbm);
	if (!status.isEmpty())
		lines << status.join(QStringLiteral(" · "));
	if (!info.appVersion.isEmpty())
		lines << QStringLiteral("OBS Mobile %1").arg(info.appVersion);
	details->setText(lines.join('\n'));

	micLevel->setValue(int(qBound(-60.0, info.micDb, 0.0)));

	if (info.preview.isNull())
		preview->setText(QStringLiteral("En attente de l'aperçu…"));
	else
		preview->setPixmap(QPixmap::fromImage(info.preview).scaled(kPreviewWidth, kPreviewHeight, Qt::KeepAspectRatio,
									  Qt::SmoothTransformation));
}

} // namespace obsmobile
