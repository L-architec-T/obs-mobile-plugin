#include "program-return.hpp"

#include "protocol.hpp"

#include <plugin-support.h>

#include <QRandomGenerator>
#include <QtEndian>

#include <algorithm>
#include <mutex>

namespace obsmobile {

namespace {

constexpr int kReturnWidth = 960;
constexpr int kReturnBitrateKbps = 1500;
constexpr int kReturnMaxFps = 30;
// A phone this far behind (slow Wi-Fi) skips to the next key frame instead of lagging more.
constexpr qint64 kMaxQueuedBytes = 512 * 1024;
// The output keeps running a little after the last phone left, so a quick come-back is instant.
constexpr int kIdleStopMs = 5'000;
constexpr int kKeyTimeoutMs = 3'000;

enum FrameKind { Header = 0, KeyFrame = 1, Frame = 2 };

// The output's packets come on an OBS thread; they are forwarded to the live ProgramReturn, if any.
std::mutex instanceMutex;
ProgramReturn *instance = nullptr;

struct ReturnOutput {
	obs_output_t *output;
};

} // namespace

ProgramReturn::ProgramReturn(QObject *parent) : QObject(parent)
{
	connect(&server, &QTcpServer::newConnection, this, &ProgramReturn::onNewConnection);
	idleStop.setSingleShot(true);
	connect(&idleStop, &QTimer::timeout, this, [this] {
		if (viewers.isEmpty() && keys.isEmpty())
			stopOutput();
	});
	std::lock_guard lock(instanceMutex);
	instance = this;
}

ProgramReturn::~ProgramReturn()
{
	{
		std::lock_guard lock(instanceMutex);
		instance = nullptr;
	}
	stopOutput();
}

bool ProgramReturn::start()
{
	if (!server.listen(QHostAddress::Any, kReturnPort)) {
		obs_log(LOG_ERROR, "Cannot listen for the video return on port %d: %s", kReturnPort,
			qPrintable(server.errorString()));
		return false;
	}
	return true;
}

QSize ProgramReturn::size() const
{
	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi) || !ovi.base_width || !ovi.base_height)
		return {kReturnWidth, kReturnWidth * 9 / 16};
	const int width = std::min<int>(kReturnWidth, ovi.base_width) & ~1;
	const int height = int(qint64(width) * ovi.base_height / ovi.base_width) & ~1;
	return {width, height};
}

QByteArray ProgramReturn::issueKey(QObject *owner)
{
	release(owner);
	QByteArray key(16, Qt::Uninitialized);
	QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(key.data()), 4);
	key = key.toHex();
	keys.insert(key, owner);
	updateOutput();
	return key;
}

void ProgramReturn::release(QObject *owner)
{
	for (auto it = keys.begin(); it != keys.end();)
		it = it.value() == owner ? keys.erase(it) : std::next(it);
	// Take the phone's sockets out first: abort() emits disconnected() at once, whose handler
	// edits [viewers] too.
	QList<QPointer<QTcpSocket>> closing;
	viewers.removeIf([&](const Viewer &v) {
		if (v.owner != owner)
			return false;
		closing.append(v.socket);
		return true;
	});
	for (const auto &socket : closing)
		if (socket)
			socket->abort();
	updateOutput();
}

void ProgramReturn::onNewConnection()
{
	while (QTcpSocket *socket = server.nextPendingConnection()) {
		socket->setParent(this);
		socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
		// Nothing but a valid key, quickly: anything else is dropped.
		QTimer::singleShot(kKeyTimeoutMs, socket, [socket] {
			if (socket->property("viewer").isNull())
				socket->abort();
		});
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onKey(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
			viewers.removeIf([socket](const Viewer &v) { return v.socket == socket; });
			socket->deleteLater();
			updateOutput();
		});
	}
}

void ProgramReturn::onKey(QTcpSocket *socket)
{
	if (!socket->property("viewer").isNull()) {
		socket->readAll(); // phones only listen once in
		return;
	}
	if (!socket->canReadLine()) {
		if (socket->bytesAvailable() > 64)
			socket->abort();
		return;
	}
	const QByteArray key = socket->readLine(64).trimmed();
	QObject *owner = keys.take(key);
	if (!owner) {
		obs_log(LOG_WARNING, "Video return: unknown key from %s", qPrintable(socket->peerAddress().toString()));
		socket->abort();
		return;
	}
	socket->setProperty("viewer", true);
	viewers.append({socket, owner, false});
	updateOutput();
}

void ProgramReturn::sendFrame(QTcpSocket *socket, int kind, qint64 ptsUs, const QByteArray &data)
{
	char head[13];
	qToBigEndian<quint32>(quint32(9 + data.size()), head);
	head[4] = char(kind);
	qToBigEndian<qint64>(ptsUs, head + 5);
	socket->write(head, sizeof(head));
	socket->write(data);
}

void ProgramReturn::onPacket(int kind, qint64 ptsUs, const QByteArray &data)
{
	if (kind == Header) {
		header = data;
		return;
	}
	for (Viewer &viewer : viewers) {
		if (!viewer.socket)
			continue;
		if (viewer.started && viewer.socket->bytesToWrite() > kMaxQueuedBytes)
			viewer.started = false;
		if (!viewer.started) {
			// A phone (re)starts on a key frame, preceded by the stream's SPS/PPS.
			if (kind != KeyFrame || header.isEmpty() || viewer.socket->bytesToWrite() > kMaxQueuedBytes)
				continue;
			sendFrame(viewer.socket, Header, ptsUs, header);
			viewer.started = true;
		}
		sendFrame(viewer.socket, kind, ptsUs, data);
	}
}

void ProgramReturn::packetCallback(void *, encoder_packet *packet)
{
	if (!packet || packet->type != OBS_ENCODER_VIDEO || !packet->size)
		return;
	const qint64 ptsUs = packet->timebase_den
				     ? packet->pts * 1'000'000 * packet->timebase_num / packet->timebase_den
				     : 0;
	std::lock_guard lock(instanceMutex);
	if (!instance)
		return;
	if (packet->keyframe) {
		uint8_t *extra = nullptr;
		size_t extraSize = 0;
		if (instance->encoder && obs_encoder_get_extra_data(instance->encoder, &extra, &extraSize) && extraSize) {
			QByteArray headerData(reinterpret_cast<const char *>(extra), qsizetype(extraSize));
			QMetaObject::invokeMethod(
				instance, [self = instance, headerData] { self->onPacket(Header, 0, headerData); },
				Qt::QueuedConnection);
		}
	}
	QByteArray data(reinterpret_cast<const char *>(packet->data), qsizetype(packet->size));
	const int kind = packet->keyframe ? KeyFrame : Frame;
	QMetaObject::invokeMethod(
		instance, [self = instance, kind, ptsUs, data] { self->onPacket(kind, ptsUs, data); },
		Qt::QueuedConnection);
}

void ProgramReturn::updateOutput()
{
	if (!viewers.isEmpty() || !keys.isEmpty()) {
		idleStop.stop();
		if (!running && !startOutput())
			stopOutput();
	} else if (running && !idleStop.isActive()) {
		idleStop.start(kIdleStopMs);
	}
}

bool ProgramReturn::startOutput()
{
	const QSize scaled = size();
	obs_video_info ovi = {};
	obs_get_video_info(&ovi);

	// Low latency first: no B-frames, a key frame every second (a phone joins within 1 s).
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CBR");
	obs_data_set_int(settings, "bitrate", kReturnBitrateKbps);
	obs_data_set_int(settings, "keyint_sec", 1);
	obs_data_set_string(settings, "preset", "veryfast");
	obs_data_set_string(settings, "profile", "baseline");
	obs_data_set_string(settings, "tune", "zerolatency");
	encoder = obs_video_encoder_create("obs_x264", "OBS Mobile · retour", settings, nullptr);
	obs_data_release(settings);
	if (!encoder) {
		obs_log(LOG_ERROR, "Video return: cannot create the x264 encoder");
		return false;
	}
	obs_encoder_set_scaled_size(encoder, uint32_t(scaled.width()), uint32_t(scaled.height()));
	if (ovi.fps_den && ovi.fps_num > uint32_t(kReturnMaxFps + 1) * ovi.fps_den) {
		const uint32_t divisor = (ovi.fps_num + kReturnMaxFps * ovi.fps_den - 1) / (kReturnMaxFps * ovi.fps_den);
		obs_encoder_set_frame_rate_divisor(encoder, divisor);
	}
	obs_encoder_set_video(encoder, obs_get_video());

	output = obs_output_create("obs_mobile_return", "OBS Mobile · retour", nullptr, nullptr);
	if (!output) {
		obs_log(LOG_ERROR, "Video return: cannot create the output");
		return false;
	}
	obs_output_set_video_encoder(output, encoder);
	if (!obs_output_start(output)) {
		const char *error = obs_output_get_last_error(output);
		obs_log(LOG_ERROR, "Video return: cannot start (%s)", error ? error : "unknown error");
		return false;
	}
	running = true;
	obs_log(LOG_INFO, "Video return started (%dx%d)", scaled.width(), scaled.height());
	return true;
}

void ProgramReturn::stopOutput()
{
	if (output) {
		obs_output_stop(output);
		obs_output_release(output);
		output = nullptr;
	}
	if (encoder) {
		std::lock_guard lock(instanceMutex); // the packet callback reads it
		obs_encoder_release(encoder);
		encoder = nullptr;
	}
	header.clear();
	for (Viewer &viewer : viewers)
		viewer.started = false;
	if (running)
		obs_log(LOG_INFO, "Video return stopped");
	running = false;
}

void registerReturnOutput()
{
	obs_output_info info = {};
	info.id = "obs_mobile_return";
	info.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED;
	info.get_name = [](void *) { return "OBS Mobile · retour"; };
	info.create = [](obs_data_t *, obs_output_t *output) -> void * { return new ReturnOutput{output}; };
	info.destroy = [](void *data) { delete static_cast<ReturnOutput *>(data); };
	info.start = [](void *data) {
		obs_output_t *output = static_cast<ReturnOutput *>(data)->output;
		if (!obs_output_can_begin_data_capture(output, 0) || !obs_output_initialize_encoders(output, 0))
			return false;
		return obs_output_begin_data_capture(output, 0);
	};
	info.stop = [](void *data, uint64_t) { obs_output_end_data_capture(static_cast<ReturnOutput *>(data)->output); };
	info.encoded_packet = [](void *data, encoder_packet *packet) { ProgramReturn::packetCallback(data, packet); };
	obs_register_output(&info);
}

} // namespace obsmobile
