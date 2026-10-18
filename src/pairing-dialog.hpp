#pragma once

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>

namespace obsmobile {

class PhoneConnection;

/**
 * "Demande de connexion": who the phone is, a live preview of its camera (not on air) and its
 * microphone level, so the streamer knows what they accept. Non-modal: OBS stays usable.
 */
class PairingDialog : public QDialog {
	Q_OBJECT
public:
	PairingDialog(PhoneConnection *phone, QWidget *parent);

private:
	void refresh();

	QPointer<PhoneConnection> phone;
	QLabel *preview;
	QLabel *title;
	QLabel *details;
	QProgressBar *micLevel;
	QCheckBox *remember;
};

} // namespace obsmobile
