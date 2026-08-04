#pragma once

/* Shared deterministic test-media generator: 2 second h264 + aac clips
 * (320x180@30) with a time-varying color, optionally with B-frames in an MKV
 * container to mimic OBS replay-buffer output. */

#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

#include <cmath>
#include <memory>

namespace MoonLitTest {

constexpr int kWidth = 320;
constexpr int kHeight = 180;
constexpr int kFps = 30;
constexpr double kDurationSeconds = 2.0;
constexpr int kSampleRate = 48000;

struct FormatContextDeleter {
	void operator()(AVFormatContext *context) const noexcept
	{
		if (context) {
			if (context->pb && !(context->oformat->flags & AVFMT_NOFILE)) {
				avio_closep(&context->pb);
			}
			avformat_free_context(context);
		}
	}
};

struct CodecContextDeleter {
	void operator()(AVCodecContext *context) const noexcept
	{
		if (context) {
			avcodec_free_context(&context);
		}
	}
};

struct FrameDeleter {
	void operator()(AVFrame *frame) const noexcept
	{
		if (frame) {
			av_frame_free(&frame);
		}
	}
};

struct PacketDeleter {
	void operator()(AVPacket *packet) const noexcept
	{
		if (packet) {
			av_packet_free(&packet);
		}
	}
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

inline bool writeEncodedFrame(AVFormatContext *output, AVCodecContext *codec, AVStream *stream, AVFrame *frame,
			      const AVRational &ptsTimeBase)
{
	PacketPtr packet(av_packet_alloc());
	if (!packet) {
		return false;
	}

	if (avcodec_send_frame(codec, frame) < 0) {
		return false;
	}
	while (avcodec_receive_packet(codec, packet.get()) == 0) {
		av_packet_rescale_ts(packet.get(), ptsTimeBase, stream->time_base);
		packet->stream_index = stream->index;
		const int result = av_interleaved_write_frame(output, packet.get());
		av_packet_unref(packet.get());
		if (result < 0) {
			return false;
		}
	}
	return true;
}

/* Encodes a 2 second h264 + aac clip with a time-varying color. When
 * `maxBFrames` is positive the output uses B-frames and an MKV container,
 * mimicking OBS replay-buffer output. */
inline bool generateTestMediaEx(const QString &path, const QString &container, int maxBFrames, bool &hasAudio,
				QString *error)
{
	AVFormatContext *rawOutput = nullptr;
	if (avformat_alloc_output_context2(&rawOutput, nullptr, container.toUtf8().constData(),
					   path.toUtf8().constData()) < 0) {
		*error = QStringLiteral("unable to create output context");
		return false;
	}
	FormatContextPtr output(rawOutput);

	const AVCodec *videoCodec = avcodec_find_encoder_by_name("libx264");
	if (!videoCodec) {
		*error = QStringLiteral("libx264 unavailable");
		return false;
	}

	AVStream *videoStream = avformat_new_stream(output.get(), videoCodec);
	CodecContextPtr video(avcodec_alloc_context3(videoCodec));
	video->width = kWidth;
	video->height = kHeight;
	video->time_base = AVRational{1, kFps};
	video->framerate = AVRational{kFps, 1};
	video->pix_fmt = AV_PIX_FMT_YUV420P;
	video->bit_rate = 400000;
	video->gop_size = kFps * 2;
	video->max_b_frames = maxBFrames;
	video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	if (avcodec_open2(video.get(), videoCodec, nullptr) < 0) {
		*error = QStringLiteral("unable to open video encoder");
		return false;
	}
	avcodec_parameters_from_context(videoStream->codecpar, video.get());
	videoStream->time_base = video->time_base;

	const AVCodec *audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	CodecContextPtr audio;
	AVStream *audioStream = nullptr;
	if (audioCodec) {
		audioStream = avformat_new_stream(output.get(), audioCodec);
		audio.reset(avcodec_alloc_context3(audioCodec));
		audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
		audio->sample_rate = kSampleRate;
		audio->bit_rate = 128000;
		av_channel_layout_default(&audio->ch_layout, 2);
		audio->time_base = AVRational{1, kSampleRate};
		audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		if (avcodec_open2(audio.get(), audioCodec, nullptr) < 0) {
			audio.reset();
			audioStream = nullptr;
		} else {
			avcodec_parameters_from_context(audioStream->codecpar, audio.get());
			audioStream->time_base = audio->time_base;
			hasAudio = true;
		}
	}

	if (avio_open(&output->pb, path.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
		*error = QStringLiteral("unable to open output file");
		return false;
	}
	const int headerResult = avformat_write_header(output.get(), nullptr);
	if (headerResult < 0) {
		char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
		av_strerror(headerResult, buffer, sizeof(buffer));
		*error = QStringLiteral("unable to write header (%1)").arg(QString::fromUtf8(buffer));
		return false;
	}

	const int frameCount = static_cast<int>(kDurationSeconds * kFps);
	for (int index = 0; index < frameCount; ++index) {
		FramePtr frame(av_frame_alloc());
		frame->format = AV_PIX_FMT_YUV420P;
		frame->width = kWidth;
		frame->height = kHeight;
		av_frame_get_buffer(frame.get(), 32);

		const double t = static_cast<double>(index) / frameCount;
		std::fill(frame->data[0], frame->data[0] + frame->linesize[0] * kHeight,
			  static_cast<uint8_t>(16 + 200 * t));
		std::fill(frame->data[1], frame->data[1] + frame->linesize[1] * (kHeight / 2), static_cast<uint8_t>(128));
		std::fill(frame->data[2], frame->data[2] + frame->linesize[2] * (kHeight / 2), static_cast<uint8_t>(128));

		frame->pts = index;
		if (!writeEncodedFrame(output.get(), video.get(), videoStream, frame.get(), AVRational{1, kFps})) {
			*error = QStringLiteral("unable to encode video frame");
			return false;
		}
	}
	writeEncodedFrame(output.get(), video.get(), videoStream, nullptr, AVRational{1, kFps});

	if (audio && audioStream) {
		const int samplesPerFrame = audio->frame_size > 0 ? audio->frame_size : 1024;
		const int audioFrames = kSampleRate * static_cast<int>(kDurationSeconds) / samplesPerFrame;
		int64_t sampleCount = 0;
		for (int index = 0; index < audioFrames; ++index) {
			FramePtr frame(av_frame_alloc());
			frame->format = AV_SAMPLE_FMT_FLTP;
			frame->sample_rate = kSampleRate;
			av_channel_layout_copy(&frame->ch_layout, &audio->ch_layout);
			frame->nb_samples = samplesPerFrame;
			av_frame_get_buffer(frame.get(), 32);

			for (int channel = 0; channel < 2; ++channel) {
				float *samples = reinterpret_cast<float *>(frame->data[channel]);
				for (int sample = 0; sample < samplesPerFrame; ++sample) {
					samples[sample] = 0.25f * std::sin(2.0 * M_PI * 440.0 *
									    (sampleCount + sample) / kSampleRate);
				}
			}
			frame->pts = sampleCount;
			sampleCount += samplesPerFrame;
			if (!writeEncodedFrame(output.get(), audio.get(), audioStream, frame.get(),
					       AVRational{1, kSampleRate})) {
				*error = QStringLiteral("unable to encode audio frame");
				return false;
			}
		}
		writeEncodedFrame(output.get(), audio.get(), audioStream, nullptr, AVRational{1, kSampleRate});
	}

	av_write_trailer(output.get());
	return true;
}

/* Encodes a 2 second h264 + aac MP4 test clip with a time-varying color. */
inline bool generateTestMedia(const QString &path, bool &hasAudio, QString *error)
{
	return generateTestMediaEx(path, QStringLiteral("mp4"), 0, hasAudio, error);
}

} // namespace MoonLitTest
