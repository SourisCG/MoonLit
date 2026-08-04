#pragma once

/* Internal FFmpeg pipeline pieces shared by the clip exporter and the
 * timeline exporter. Header-only, never installed. */

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTextStream>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace MoonLit {
namespace ffmpeg {

constexpr int kAacSampleRate = 48000;
constexpr int kAacBitRate = 192000;

struct FormatContextDeleter {
	void operator()(AVFormatContext *context) const noexcept
	{
		if (context) {
			avformat_close_input(&context);
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

struct SwrDeleter {
	void operator()(SwrContext *context) const noexcept
	{
		if (context) {
			swr_free(&context);
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
using SwrContextPtr = std::unique_ptr<SwrContext, SwrDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

struct OutputContext {
	AVFormatContext *context{nullptr};
	bool headerWritten{false};

	OutputContext() = default;

	~OutputContext() noexcept { close(); }

	OutputContext(const OutputContext &) = delete;
	OutputContext &operator=(const OutputContext &) = delete;

	void close() noexcept
	{
		if (!context) {
			return;
		}

		if (headerWritten) {
			av_write_trailer(context);
		}
		if (context->pb && !(context->oformat->flags & AVFMT_NOFILE)) {
			avio_closep(&context->pb);
		}
		avformat_free_context(context);
		context = nullptr;
		headerWritten = false;
	}
};

inline QString ffmpegError(int code)
{
	char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
	if (av_strerror(code, buffer, sizeof(buffer)) < 0) {
		return QStringLiteral("FFmpeg error %1").arg(code);
	}

	return QString::fromUtf8(buffer);
}

inline QString normalizedPath(const QString &path)
{
	const QFileInfo fileInfo(path);
	const QString canonicalPath = fileInfo.canonicalFilePath();
	return QDir::cleanPath(canonicalPath.isEmpty() ? fileInfo.absoluteFilePath() : canonicalPath);
}

inline bool samePath(const QString &left, const QString &right)
{
	const QString normalizedLeft = normalizedPath(left);
	const QString normalizedRight = normalizedPath(right);
#ifdef Q_OS_WIN
	return QString::compare(normalizedLeft, normalizedRight, Qt::CaseInsensitive) == 0;
#else
	return normalizedLeft == normalizedRight;
#endif
}

inline int64_t packetTimestampUs(const AVPacket &packet, const AVStream &stream)
{
	const int64_t timestamp = packet.dts != AV_NOPTS_VALUE ? packet.dts : packet.pts;
	if (timestamp == AV_NOPTS_VALUE) {
		return AV_NOPTS_VALUE;
	}

	return av_rescale_q(timestamp, stream.time_base, AV_TIME_BASE_Q);
}

inline qint64 mediaDurationMs(const AVFormatContext &context)
{
	if (context.duration != AV_NOPTS_VALUE) {
		return av_rescale_q(context.duration, AV_TIME_BASE_Q, AVRational{1, 1000});
	}

	for (unsigned int index = 0; index < context.nb_streams; ++index) {
		const AVStream *stream = context.streams[index];
		if (stream->duration != AV_NOPTS_VALUE) {
			return av_rescale_q(stream->duration, stream->time_base, AVRational{1, 1000});
		}
	}

	return -1;
}

inline bool verifyOutput(const QString &path, qint64 &durationMs, QString &error)
{
	AVFormatContext *rawContext = nullptr;
	const QByteArray encodedPath = QFile::encodeName(path);
	const int openResult = avformat_open_input(&rawContext, encodedPath.constData(), nullptr, nullptr);
	if (openResult < 0) {
		error = QStringLiteral("Unable to verify export: %1").arg(ffmpegError(openResult));
		return false;
	}

	FormatContextPtr context(rawContext);
	const int streamInfoResult = avformat_find_stream_info(context.get(), nullptr);
	if (streamInfoResult < 0) {
		error = QStringLiteral("Unable to verify exported streams: %1").arg(ffmpegError(streamInfoResult));
		return false;
	}
	if (context->nb_streams == 0) {
		error = QStringLiteral("Export contains no media streams");
		return false;
	}

	durationMs = mediaDurationMs(*context);
	return true;
}

inline void appendExportLog(const QString &destinationPath, const QString &line)
{
	const QFileInfo destinationInfo(destinationPath);
	if (destinationInfo.absolutePath().isEmpty()) {
		return;
	}
	if (!QDir().mkpath(destinationInfo.absolutePath())) {
		return;
	}

	QFile log(QDir(destinationInfo.absolutePath()).filePath(QStringLiteral("export.log")));
	if (!log.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) {
		return;
	}

	QTextStream stream(&log);
	stream << QDateTime::currentDateTime().toString(Qt::ISODate) << QLatin1Char(' ') << line << QLatin1Char('\n');
}

/* Re-encodes one audio stream to apply mute/gain. The output is a plain AAC
 * stream at 48 kHz stereo (mono input stays mono). The timeline exporter can
 * pin the output offset with setOutputOffset() so concatenated segments stay
 * contiguous. */
struct AudioEncodeStream {
	int inputStreamIndex = -1;
	AVStream *outputStream = nullptr;

	CodecContextPtr decoder;
	CodecContextPtr encoder;
	SwrContextPtr resampler;
	FramePtr decoded;
	FramePtr converted;
	PacketPtr encodedPacket;

	bool finished = false;
	int64_t offsetUs = AV_NOPTS_VALUE;
	int64_t framePtsOffsetUs = 0;

	AudioEncodeStream() = default;
	~AudioEncodeStream() = default;

	AudioEncodeStream(const AudioEncodeStream &) = delete;
	AudioEncodeStream &operator=(const AudioEncodeStream &) = delete;
	AudioEncodeStream(AudioEncodeStream &&) noexcept = default;
	AudioEncodeStream &operator=(AudioEncodeStream &&) noexcept = default;

	bool initialize(const AVStream *inputStream, AVStream *output, const AVCodec *aacEncoder, QString *error)
	{
		inputStreamIndex = inputStream->index;
		outputStream = output;

		const AVCodec *decoderCodec = avcodec_find_decoder(inputStream->codecpar->codec_id);
		if (!decoderCodec) {
			*error = QStringLiteral("No audio decoder available for the source");
			return false;
		}

		decoder.reset(avcodec_alloc_context3(decoderCodec));
		if (!decoder) {
			*error = QStringLiteral("Unable to allocate audio decoder");
			return false;
		}
		if (avcodec_parameters_to_context(decoder.get(), inputStream->codecpar) < 0) {
			*error = QStringLiteral("Unable to configure audio decoder");
			return false;
		}
		if (avcodec_open2(decoder.get(), decoderCodec, nullptr) < 0) {
			*error = QStringLiteral("Unable to open audio decoder");
			return false;
		}

		const int channels = std::min<int>(decoder->ch_layout.nb_channels > 0 ? decoder->ch_layout.nb_channels : 2,
						   2);
		encoder.reset(avcodec_alloc_context3(aacEncoder));
		if (!encoder) {
			*error = QStringLiteral("Unable to allocate audio encoder");
			return false;
		}
		encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
		encoder->sample_rate = kAacSampleRate;
		encoder->bit_rate = kAacBitRate;
		av_channel_layout_default(&encoder->ch_layout, channels);
		encoder->time_base = AVRational{1, kAacSampleRate};
		if (avcodec_open2(encoder.get(), aacEncoder, nullptr) < 0) {
			*error = QStringLiteral("Unable to open audio encoder");
			return false;
		}

		avcodec_parameters_from_context(output->codecpar, encoder.get());
		output->codecpar->codec_tag = 0;
		output->time_base = encoder->time_base;

		AVChannelLayout outputLayout;
		av_channel_layout_default(&outputLayout, channels);
		SwrContext *rawResampler = nullptr;
		swr_alloc_set_opts2(&rawResampler, &outputLayout, AV_SAMPLE_FMT_FLTP, kAacSampleRate,
				    &decoder->ch_layout, decoder->sample_fmt, decoder->sample_rate, 0, nullptr);
		resampler.reset(rawResampler);
		if (!resampler || swr_init(resampler.get()) < 0) {
			*error = QStringLiteral("Unable to initialize audio resampler");
			return false;
		}
		av_channel_layout_uninit(&outputLayout);

		decoded.reset(av_frame_alloc());
		converted.reset(av_frame_alloc());
		encodedPacket.reset(av_packet_alloc());
		if (!decoded || !converted || !encodedPacket) {
			*error = QStringLiteral("Unable to allocate audio encode buffers");
			return false;
		}
		return true;
	}

	/* Pins the output timestamp origin so concatenated segments stay
	 * contiguous (offset = segmentStartUs - timelineStartUs). */
	void setOutputOffset(int64_t outputOffsetUs) { offsetUs = outputOffsetUs; }

	/* Timeline mode: re-bases frame timestamps to the timeline before they
	 * enter the shared encoder, so delayed encoder packets stay monotonic. */
	void setFramePtsOffset(int64_t offsetUs) { framePtsOffsetUs = offsetUs; }

	/* Rebuilds the per-segment decoder/resampler while keeping the shared
	 * AAC encoder. Call before every timeline segment's packets. */
	bool initializeSegment(const AVStream *inputStream, QString *error)
	{
		const AVCodec *decoderCodec = avcodec_find_decoder(inputStream->codecpar->codec_id);
		if (!decoderCodec) {
			*error = QStringLiteral("No audio decoder available for the source");
			return false;
		}

		decoder.reset(avcodec_alloc_context3(decoderCodec));
		if (!decoder) {
			*error = QStringLiteral("Unable to allocate audio decoder");
			return false;
		}
		if (avcodec_parameters_to_context(decoder.get(), inputStream->codecpar) < 0) {
			*error = QStringLiteral("Unable to configure audio decoder");
			return false;
		}
		if (avcodec_open2(decoder.get(), decoderCodec, nullptr) < 0) {
			*error = QStringLiteral("Unable to open audio decoder");
			return false;
		}

		const int channels = std::min<int>(decoder->ch_layout.nb_channels > 0 ? decoder->ch_layout.nb_channels : 2,
						   2);
		AVChannelLayout outputLayout;
		av_channel_layout_default(&outputLayout, channels);
		SwrContext *rawResampler = nullptr;
		swr_alloc_set_opts2(&rawResampler, &outputLayout, AV_SAMPLE_FMT_FLTP, kAacSampleRate,
				    &decoder->ch_layout, decoder->sample_fmt, decoder->sample_rate, 0, nullptr);
		resampler.reset(rawResampler);
		if (!resampler || swr_init(resampler.get()) < 0) {
			*error = QStringLiteral("Unable to initialize audio resampler");
			return false;
		}
		av_channel_layout_uninit(&outputLayout);

		finished = false;
		return true;
	}

	/* Writes one encoded packet (shifted to the output origin). Returns 0
	 * when a packet was written, 1 when the encoder has nothing buffered
	 * right now (EAGAIN/EOF, not an error), or a negative FFmpeg error. */
	int writeEncodedPacket(AVFormatContext *output, double gain)
	{
		const int receiveResult = avcodec_receive_packet(encoder.get(), encodedPacket.get());
		if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
			return 1;
		}
		if (receiveResult < 0) {
			return receiveResult;
		}

		av_packet_rescale_ts(encodedPacket.get(), encoder->time_base, outputStream->time_base);
		const int64_t timestamp = encodedPacket->dts != AV_NOPTS_VALUE ? encodedPacket->dts : encodedPacket->pts;
		if (timestamp != AV_NOPTS_VALUE && offsetUs != AV_NOPTS_VALUE) {
			const int64_t shiftedUs = std::max<int64_t>(
				0, av_rescale_q(timestamp, outputStream->time_base, AV_TIME_BASE_Q) - offsetUs);
			const int64_t shifted = av_rescale_q(shiftedUs, AV_TIME_BASE_Q, outputStream->time_base);
			if (encodedPacket->pts != AV_NOPTS_VALUE) {
				encodedPacket->pts = shifted;
			}
			if (encodedPacket->dts != AV_NOPTS_VALUE) {
				encodedPacket->dts = shifted;
			}
		}
		encodedPacket->stream_index = outputStream->index;

		const int writeResult = av_interleaved_write_frame(output, encodedPacket.get());
		if (writeResult < 0) {
			return writeResult;
		}
		av_packet_unref(encodedPacket.get());
		return writeResult;
	}

	/* Encodes whatever the encoder has buffered (flush or after a frame). */
	int encodeAvailable(AVFormatContext *output, double gain)
	{
		int result;
		while ((result = writeEncodedPacket(output, gain)) == 0) {
		}
		return result < 0 ? result : 0;
	}

	/* Feeds one decoded input frame through the resampler (applying mute/gain)
	 * into the encoder. Returns 0 on success or a negative error code. */
	int processFrame(AVFormatContext *output, const AVStream *inputStream, int64_t endUs, double gain,
			 bool &endReached)
	{
		const int64_t frameUs = decoded->pts != AV_NOPTS_VALUE
						? av_rescale_q(decoded->pts, inputStream->time_base, AV_TIME_BASE_Q)
						: AV_NOPTS_VALUE;
		if (offsetUs == AV_NOPTS_VALUE && frameUs != AV_NOPTS_VALUE) {
			offsetUs = frameUs;
		}
		if (endUs != AV_NOPTS_VALUE && frameUs != AV_NOPTS_VALUE && frameUs >= endUs) {
			endReached = true;
			finished = true;
			return 0;
		}

		const int outSamples =
			swr_get_out_samples(resampler.get(), decoded->nb_samples);
		av_frame_unref(converted.get());
		converted->sample_rate = kAacSampleRate;
		converted->format = AV_SAMPLE_FMT_FLTP;
		av_channel_layout_copy(&converted->ch_layout, &encoder->ch_layout);
		converted->nb_samples = outSamples;
		if (av_frame_get_buffer(converted.get(), 0) < 0) {
			return AVERROR(ENOMEM);
		}
		if (swr_convert(resampler.get(), converted->data, converted->nb_samples, decoded->data,
				decoded->nb_samples) < 0) {
			return AVERROR(EINVAL);
		}

		if (decoded->pts != AV_NOPTS_VALUE) {
			const int64_t sourceUs = av_rescale_q(decoded->pts, inputStream->time_base, AV_TIME_BASE_Q);
			converted->pts = av_rescale_q(sourceUs + framePtsOffsetUs, AV_TIME_BASE_Q, encoder->time_base);
		}

		if (gain <= 0.0) {
			for (int channel = 0; channel < converted->ch_layout.nb_channels; ++channel) {
				if (converted->data[channel]) {
					std::fill_n(reinterpret_cast<float *>(converted->data[channel]),
						    converted->nb_samples, 0.0f);
				}
			}
		} else if (gain != 1.0) {
			for (int channel = 0; channel < converted->ch_layout.nb_channels; ++channel) {
				float *samples = reinterpret_cast<float *>(converted->data[channel]);
				if (!samples) {
					continue;
				}
				for (int index = 0; index < converted->nb_samples; ++index) {
					samples[index] *= static_cast<float>(gain);
				}
			}
		}

		int sendResult = avcodec_send_frame(encoder.get(), converted.get());
		if (sendResult == AVERROR(EAGAIN)) {
			const int drain = encodeAvailable(output, gain);
			if (drain < 0) {
				return drain;
			}
			sendResult = avcodec_send_frame(encoder.get(), converted.get());
		}
		if (sendResult < 0) {
			return sendResult;
		}
		return encodeAvailable(output, gain);
	}

	/* Pushes one input packet into the decoder and encodes whatever comes
	 * out. Returns 0 on success or a negative FFmpeg error code. */
	int pushPacket(AVPacket *inputPacket, AVFormatContext *output, const AVStream *inputStream, int64_t endUs,
		       double gain, bool &endReached)
	{
		if (finished) {
			return 0;
		}

		const int sendResult = avcodec_send_packet(decoder.get(), inputPacket);
		if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
			return sendResult;
		}
		while (avcodec_receive_frame(decoder.get(), decoded.get()) == 0) {
			int result = processFrame(output, inputStream, endUs, gain, endReached);
			av_frame_unref(decoded.get());
			if (result < 0) {
				return result;
			}
			if (endReached) {
				return 0;
			}
		}
		return 0;
	}

	/* Flushes the decoder (and, for the final segment, the shared encoder) at
	 * end of input. When `endUs` is set, frames at or beyond it are dropped
	 * (timeline segment boundary). The encoder must not be flushed between
	 * timeline segments: a flushed encoder rejects further input with EOF. */
	int drain(AVFormatContext *output, double gain, int64_t endUs = AV_NOPTS_VALUE, bool flushEncoder = true)
	{
		if (finished) {
			return 0;
		}
		finished = true;

		if (avcodec_send_packet(decoder.get(), nullptr) >= 0) {
			while (avcodec_receive_frame(decoder.get(), decoded.get()) == 0) {
				bool endReached = false;
				const int result = processFrame(output, nullptr, endUs, gain, endReached);
				av_frame_unref(decoded.get());
				if (result < 0) {
					return result;
				}
				if (endReached) {
					break;
				}
			}
		}

		if (flushEncoder && avcodec_send_frame(encoder.get(), nullptr) >= 0) {
			int result = encodeAvailable(output, gain);
			if (result < 0) {
				return result;
			}
		}
		return 0;
	}
};

} // namespace ffmpeg
} // namespace MoonLit
