#include "mobile-camera-source.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>
#include <util/threading.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
}

#include <atomic>
#include <string>
#include <thread>

/*
 * A phone camera received over SRT (MPEG-TS, H.264 + AAC), with latency kept to the network:
 * - FFmpeg is opened without buffering and with a short probe;
 * - each frame goes to OBS as soon as it is decoded, stamped with the current time, and the
 *   source is "async unbuffered": OBS shows the newest frame instead of scheduling by PTS;
 * - when the stream stops the source is cleared (transparent), never frozen.
 */

namespace obsmobile {

namespace {

constexpr int kLatencyMs = 80;
constexpr uint64_t kReceivingTimeoutNs = 1'000'000'000ULL;

struct CameraSource {
	obs_source_t *source = nullptr;
	int port = 0;
	std::thread worker;
	std::atomic<bool> stopping{false};
	std::atomic<uint64_t> lastFrameNs{0};

	void start()
	{
		stopping = false;
		worker = std::thread([this] { run(); });
	}

	void stop()
	{
		stopping = true;
		if (worker.joinable())
			worker.join();
	}

	static int interrupt(void *opaque) { return static_cast<CameraSource *>(opaque)->stopping.load() ? 1 : 0; }

	void run()
	{
		os_set_thread_name("obs-mobile: camera");
		while (!stopping) {
			receiveOnce();
			// Stream over or failed: never leave the last picture on screen.
			obs_source_output_video(source, nullptr);
			lastFrameNs = 0;
			if (!stopping)
				os_sleep_ms(200);
		}
	}

	void receiveOnce()
	{
		const std::string url = "srt://0.0.0.0:" + std::to_string(port) +
					"?mode=listener&latency=" + std::to_string(kLatencyMs * 1000);
		AVFormatContext *fmt = avformat_alloc_context();
		fmt->interrupt_callback = {&CameraSource::interrupt, this};
		fmt->flags |= AVFMT_FLAG_NOBUFFER;
		AVDictionary *opts = nullptr;
		av_dict_set(&opts, "analyzeduration", "500000", 0);
		av_dict_set(&opts, "probesize", "500000", 0);
		const int opened = avformat_open_input(&fmt, url.c_str(), av_find_input_format("mpegts"), &opts);
		av_dict_free(&opts);
		if (opened < 0)
			return; // avformat_open_input frees fmt on failure
		if (avformat_find_stream_info(fmt, nullptr) < 0) {
			avformat_close_input(&fmt);
			return;
		}
		obs_log(LOG_INFO, "Camera on port %d: phone connected", port);

		AVCodecContext *video = openDecoder(fmt, AVMEDIA_TYPE_VIDEO);
		AVCodecContext *audio = openDecoder(fmt, AVMEDIA_TYPE_AUDIO);
		const int videoIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		const int audioIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

		AVPacket *packet = av_packet_alloc();
		AVFrame *frame = av_frame_alloc();
		while (!stopping && av_read_frame(fmt, packet) >= 0) {
			if (packet->stream_index == videoIndex && video)
				decode(video, packet, frame, true);
			else if (packet->stream_index == audioIndex && audio)
				decode(audio, packet, frame, false);
			av_packet_unref(packet);
		}
		obs_log(LOG_INFO, "Camera on port %d: stream ended", port);

		av_frame_free(&frame);
		av_packet_free(&packet);
		avcodec_free_context(&video);
		avcodec_free_context(&audio);
		avformat_close_input(&fmt);
	}

	static AVCodecContext *openDecoder(AVFormatContext *fmt, AVMediaType type)
	{
		const int index = av_find_best_stream(fmt, type, -1, -1, nullptr, 0);
		if (index < 0)
			return nullptr;
		const AVCodecParameters *params = fmt->streams[index]->codecpar;
		const AVCodec *codec = avcodec_find_decoder(params->codec_id);
		if (!codec)
			return nullptr;
		AVCodecContext *ctx = avcodec_alloc_context3(codec);
		avcodec_parameters_to_context(ctx, params);
		if (type == AVMEDIA_TYPE_VIDEO) {
			// Frame threading holds several frames back: slice threading only, low delay.
			ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
			ctx->thread_type = FF_THREAD_SLICE;
		}
		if (avcodec_open2(ctx, codec, nullptr) < 0) {
			avcodec_free_context(&ctx);
			return nullptr;
		}
		return ctx;
	}

	void decode(AVCodecContext *ctx, AVPacket *packet, AVFrame *frame, bool isVideo)
	{
		if (avcodec_send_packet(ctx, packet) < 0)
			return;
		while (avcodec_receive_frame(ctx, frame) >= 0) {
			if (isVideo)
				outputVideo(frame);
			else
				outputAudio(frame);
			av_frame_unref(frame);
		}
	}

	void outputVideo(const AVFrame *f)
	{
		video_format format;
		switch (f->format) {
		case AV_PIX_FMT_YUV420P:
		case AV_PIX_FMT_YUVJ420P:
			format = VIDEO_FORMAT_I420;
			break;
		case AV_PIX_FMT_NV12:
			format = VIDEO_FORMAT_NV12;
			break;
		default:
			return; // phones send yuv420p H.264
		}
		obs_source_frame out = {};
		for (int i = 0; i < MAX_AV_PLANES && i < AV_NUM_DATA_POINTERS; ++i) {
			out.data[i] = f->data[i];
			out.linesize[i] = uint32_t(f->linesize[i]);
		}
		out.width = uint32_t(f->width);
		out.height = uint32_t(f->height);
		out.format = format;
		out.full_range = f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P;
		const video_colorspace cs = f->colorspace == AVCOL_SPC_BT709 ? VIDEO_CS_709 : VIDEO_CS_601;
		video_format_get_parameters(cs, out.full_range ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL, out.color_matrix,
					    out.color_range_min, out.color_range_max);
		const uint64_t now = os_gettime_ns();
		out.timestamp = now;
		obs_source_output_video(source, &out);
		lastFrameNs = now;
	}

	void outputAudio(const AVFrame *f)
	{
		audio_format format;
		switch (f->format) {
		case AV_SAMPLE_FMT_FLTP:
			format = AUDIO_FORMAT_FLOAT_PLANAR;
			break;
		case AV_SAMPLE_FMT_FLT:
			format = AUDIO_FORMAT_FLOAT;
			break;
		case AV_SAMPLE_FMT_S16P:
			format = AUDIO_FORMAT_16BIT_PLANAR;
			break;
		case AV_SAMPLE_FMT_S16:
			format = AUDIO_FORMAT_16BIT;
			break;
		default:
			return;
		}
		const int channels = f->ch_layout.nb_channels;
		obs_source_audio out = {};
		for (int i = 0; i < MAX_AV_PLANES && i < AV_NUM_DATA_POINTERS; ++i)
			out.data[i] = f->data[i];
		out.frames = uint32_t(f->nb_samples);
		out.speakers = channels >= 2 ? SPEAKERS_STEREO : SPEAKERS_MONO;
		out.format = format;
		out.samples_per_sec = uint32_t(f->sample_rate);
		out.timestamp = os_gettime_ns();
		obs_source_output_audio(source, &out);
	}
};

const char *getName(void *)
{
	return "OBS Mobile · caméra";
}

void *create(obs_data_t *settings, obs_source_t *source)
{
	auto *self = new CameraSource;
	self->source = source;
	self->port = int(obs_data_get_int(settings, "port"));
	// Show the newest frame right away instead of scheduling frames by their timestamps.
	obs_source_set_async_unbuffered(source, true);
	if (self->port > 0)
		self->start();
	return self;
}

void destroy(void *data)
{
	auto *self = static_cast<CameraSource *>(data);
	self->stop();
	delete self;
}

void update(void *data, obs_data_t *settings)
{
	auto *self = static_cast<CameraSource *>(data);
	const int port = int(obs_data_get_int(settings, "port"));
	if (port == self->port)
		return;
	self->stop();
	self->port = port;
	if (port > 0)
		self->start();
}

void getDefaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "port", 0);
}

} // namespace

void registerCameraSource()
{
	obs_source_info info = {};
	info.id = kCameraSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	// Created and placed by the plugin for each phone: not offered in "Add source".
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CAP_DISABLED;
	info.get_name = getName;
	info.create = create;
	info.destroy = destroy;
	info.update = update;
	info.get_defaults = getDefaults;
	info.icon_type = OBS_ICON_TYPE_CAMERA;
	obs_register_source(&info);
}

bool cameraIsReceiving(obs_source_t *source)
{
	if (!source || strcmp(obs_source_get_unversioned_id(source), kCameraSourceId) != 0)
		return false;
	auto *self = static_cast<CameraSource *>(obs_obj_get_data(source));
	if (!self)
		return false;
	const uint64_t last = self->lastFrameNs.load();
	return last != 0 && os_gettime_ns() - last < kReceivingTimeoutNs;
}

} // namespace obsmobile
