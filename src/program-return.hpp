#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <obs.h>

namespace obsmobile {

/**
 * Video return: what OBS has on air (its main output), encoded in H.264 by OBS's own x264 at
 * about 540p, sent to the phones that ask for it on [kReturnPort]. A dedicated output runs only
 * while at least one phone watches; the stream and recordings are not touched. UI thread only,
 * except the packet callback which hops onto it.
 *
 * Wire format, per frame: u32 big-endian length (of what follows), u8 kind (0 = SPS/PPS,
 * 1 = key frame, 2 = frame), i64 big-endian pts in µs, then the Annex B H.264 data.
 */
class ProgramReturn : public QObject {
	Q_OBJECT
public:
	explicit ProgramReturn(QObject *parent = nullptr);
	~ProgramReturn() override;

	bool start();

	/** One-time key a phone presents on the return port; invalid once used. */
	QByteArray issueKey(QObject *owner);
	/** Drops the phone's return (and any key not used yet). */
	void release(QObject *owner);

	QSize size() const;

	static void packetCallback(void *data, encoder_packet *packet);

private:
	struct Viewer {
		QPointer<QTcpSocket> socket;
		QObject *owner = nullptr;
		bool started = false; // first key frame sent
	};

	void onNewConnection();
	void onKey(QTcpSocket *socket);
	void onPacket(int kind, qint64 ptsUs, const QByteArray &data);
	void sendFrame(QTcpSocket *socket, int kind, qint64 ptsUs, const QByteArray &data);
	void updateOutput();
	bool startOutput();
	void stopOutput();

	QTcpServer server;
	QHash<QByteArray, QObject *> keys;
	QList<Viewer> viewers;
	QByteArray header;
	QTimer idleStop;
	obs_output_t *output = nullptr;
	obs_encoder_t *encoder = nullptr;
	bool running = false;
};

/** Registers the "obs_mobile_return" output type. Called once at module load. */
void registerReturnOutput();

} // namespace obsmobile
