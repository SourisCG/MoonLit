#include "ClipExportService.hpp"

#include "ExportMath.hpp"

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

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace MoonLit {
namespace {

/* Keyframe-aligned trimming starts at a keyframe at or before the requested
 * point, so the export may be up to one GOP longer than the range; it should
 * never be meaningfully shorter (that indicates dropped packets). */
constexpr qint64 kTrimStartToleranceMs = 500;
constexpr qint64 kTrimEndToleranceMs = 10000;

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

QString ffmpegError(int code)
{
	char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
	if (av_strerror(code, buffer, sizeof(buffer)) < 0) {
		return QStringLiteral("FFmpeg error %1").arg(code);
	}

	return QString::fromUtf8(buffer);
}

QString normalizedPath(const QString &path)
{
	const QFileInfo fileInfo(path);
	const QString canonicalPath = fileInfo.canonicalFilePath();
	return QDir::cleanPath(canonicalPath.isEmpty() ? fileInfo.absoluteFilePath() : canonicalPath);
}

bool samePath(const QString &left, const QString &right)
{
	const QString normalizedLeft = normalizedPath(left);
	const QString normalizedRight = normalizedPath(right);
#ifdef Q_OS_WIN
	return QString::compare(normalizedLeft, normalizedRight, Qt::CaseInsensitive) == 0;
#else
	return normalizedLeft == normalizedRight;
#endif
}

void setError(ClipExportResult &result, const QString &message)
{
	result.error = message;
}

int64_t packetTimestampUs(const AVPacket &packet, const AVStream &stream)
{
	const int64_t timestamp = packet.dts != AV_NOPTS_VALUE ? packet.dts : packet.pts;
	if (timestamp == AV_NOPTS_VALUE) {
		return AV_NOPTS_VALUE;
	}

	return av_rescale_q(timestamp, stream.time_base, AV_TIME_BASE_Q);
}

qint64 mediaDurationMs(const AVFormatContext &context)
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

bool verifyOutput(const QString &path, qint64 &durationMs, QString &error)
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

/* Re-encodes one audio stream to apply mute/gain. The output is a plain AAC
 * stream at 48 kHz stereo (mono input stays mono). */
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

	/* Writes one encoded packet (shifted to start at zero). Returns 0 when a
	 * packet was written, 1 when the encoder has nothing buffered right now
	 * (EAGAIN/EOF, not an error), or a negative FFmpeg error code. */
	int writeEncodedPacket(AVFormatContext *output, const AVStream *inputStream, double gain)
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
		av_packet_unref(encodedPacket.get());
		return writeResult;
	}

	/* Encodes whatever the encoder has buffered (flush or after a frame). */
	int encodeAvailable(AVFormatContext *output, const AVStream *inputStream, double gain)
	{
		int result;
		while ((result = writeEncodedPacket(output, inputStream, gain)) == 0) {
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
			converted->pts = av_rescale_q(decoded->pts, inputStream->time_base, encoder->time_base);
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
			const int drain = encodeAvailable(output, inputStream, gain);
			if (drain < 0) {
				return drain;
			}
			sendResult = avcodec_send_frame(encoder.get(), converted.get());
		}
		if (sendResult < 0) {
			return sendResult;
		}
		return encodeAvailable(output, inputStream, gain);
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

	/* Flushes the decoder and encoder at end of input. */
	int drain(AVFormatContext *output, const AVStream *inputStream, double gain)
	{
		if (finished) {
			return 0;
		}
		finished = true;

		if (avcodec_send_packet(decoder.get(), nullptr) >= 0) {
			while (avcodec_receive_frame(decoder.get(), decoded.get()) == 0) {
				bool endReached = false;
				const int result = processFrame(output, inputStream, AV_NOPTS_VALUE, gain, endReached);
				av_frame_unref(decoded.get());
				if (result < 0) {
					return result;
				}
			}
		}

		if (avcodec_send_frame(encoder.get(), nullptr) >= 0) {
			int result = encodeAvailable(output, inputStream, gain);
			if (result < 0) {
				return result;
			}
		}
		return 0;
	}
};

} // namespace

ClipExportResult FfmpegClipExportService::exportClip(const ClipExportRequest &request,
						     CancelCallback shouldCancel) const
{
	ClipExportResult result;
	result.outputPath = request.destinationPath;

	if (request.sourcePath.isEmpty() || request.destinationPath.isEmpty()) {
		setError(result, QStringLiteral("Source and destination paths are required"));
		return result;
	}
	if (!exportmath::isRangeValid(request.startMs, request.endMs)) {
		setError(result, QStringLiteral("Invalid export range"));
		return result;
	}
	if (samePath(request.sourcePath, request.destinationPath)) {
		setError(result, QStringLiteral("Export destination must differ from the source"));
		return result;
	}
	if (shouldCancel && shouldCancel()) {
		result.cancelled = true;
		setError(result, QStringLiteral("Export cancelled"));
		return result;
	}

	const QFileInfo sourceInfo(request.sourcePath);
	if (!sourceInfo.exists() || !sourceInfo.isFile()) {
		setError(result, QStringLiteral("Source media file does not exist: %1").arg(request.sourcePath));
		return result;
	}
	if (QFile::exists(request.destinationPath)) {
		setError(result, QStringLiteral("Export destination already exists: %1").arg(request.destinationPath));
		return result;
	}

	const QFileInfo destinationInfo(request.destinationPath);
	if (!QDir().mkpath(destinationInfo.absolutePath())) {
		setError(result,
			 QStringLiteral("Unable to create export directory: %1").arg(destinationInfo.absolutePath()));
		return result;
	}

	AVFormatContext *rawInput = nullptr;
	const QByteArray encodedSource = QFile::encodeName(sourceInfo.absoluteFilePath());
	const int inputOpenResult = avformat_open_input(&rawInput, encodedSource.constData(), nullptr, nullptr);
	if (inputOpenResult < 0) {
		setError(result, QStringLiteral("Unable to open source media: %1").arg(ffmpegError(inputOpenResult)));
		return result;
	}

	FormatContextPtr input(rawInput);
	const int inputInfoResult = avformat_find_stream_info(input.get(), nullptr);
	if (inputInfoResult < 0) {
		setError(result, QStringLiteral("Unable to read source streams: %1").arg(ffmpegError(inputInfoResult)));
		return result;
	}

	const QString partPath = request.destinationPath + QStringLiteral(".part");
	if (samePath(request.sourcePath, partPath)) {
		setError(result, QStringLiteral("Export temporary path must differ from the source"));
		return result;
	}
	if (QFile::exists(partPath) && !QFile::remove(partPath)) {
		setError(result, QStringLiteral("Unable to remove previous partial export: %1").arg(partPath));
		return result;
	}

	const bool applyAudioEdits = request.muted || request.gainDb != 0.0;
	const AVCodec *aacEncoder = nullptr;
	if (applyAudioEdits) {
		aacEncoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
		if (!aacEncoder) {
			setError(result,
				 QStringLiteral("Audio editing requires an AAC encoder, which is unavailable"));
			return result;
		}
	}
	const double gain = request.muted ? 0.0 : exportmath::linearGainDb(request.gainDb);

	// The source is read-only; incomplete output stays in the sibling .part file.
	OutputContext output;
	auto fail = [&](const QString &message, bool cancelled = false) {
		output.close();
		QFile::remove(partPath);
		result.cancelled = cancelled;
		setError(result, message);
		return result;
	};
	const QByteArray encodedDestination = QFile::encodeName(request.destinationPath);
	int outputContextResult =
		avformat_alloc_output_context2(&output.context, nullptr, nullptr, encodedDestination.constData());
	if (outputContextResult < 0 || !output.context) {
		return fail(
			QStringLiteral("Unable to select export container: %1").arg(ffmpegError(outputContextResult)));
	}

	std::vector<AVStream *> outputStreams(input->nb_streams, nullptr);
	std::vector<AudioEncodeStream> audioStreams;
	for (unsigned int index = 0; index < input->nb_streams; ++index) {
		const AVStream *inputStream = input->streams[index];
		AVStream *outputStream = avformat_new_stream(output.context, nullptr);
		if (!outputStream) {
			return fail(QStringLiteral("Unable to allocate export stream"));
		}

		const bool reencodeAudio =
			applyAudioEdits && inputStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
		if (reencodeAudio) {
			AudioEncodeStream audio;
			QString audioError;
			if (!audio.initialize(inputStream, outputStream, aacEncoder, &audioError)) {
				return fail(audioError);
			}
			audioStreams.push_back(std::move(audio));
			outputStreams[index] = outputStream;
			continue;
		}

		const int copyResult = avcodec_parameters_copy(outputStream->codecpar, inputStream->codecpar);
		if (copyResult < 0) {
			return fail(QStringLiteral("Unable to copy export stream parameters: %1")
					    .arg(ffmpegError(copyResult)));
		}
		outputStream->codecpar->codec_tag = 0;
		outputStream->time_base = inputStream->time_base;
		outputStreams[index] = outputStream;
	}

	if (!(output.context->oformat->flags & AVFMT_NOFILE)) {
		const QByteArray encodedPart = QFile::encodeName(partPath);
		const int ioResult = avio_open(&output.context->pb, encodedPart.constData(), AVIO_FLAG_WRITE);
		if (ioResult < 0) {
			return fail(QStringLiteral("Unable to create partial export: %1").arg(ffmpegError(ioResult)));
		}
	}

	const int headerResult = avformat_write_header(output.context, nullptr);
	if (headerResult < 0) {
		return fail(QStringLiteral("Unable to write export header: %1").arg(ffmpegError(headerResult)));
	}
	output.headerWritten = true;

	if (request.startMs > 0) {
		// Fast trim seeks backward to a keyframe and therefore does not modify the source.
		const int64_t startUs = av_rescale_q(request.startMs, AVRational{1, 1000}, AV_TIME_BASE_Q);
		const int seekResult = avformat_seek_file(input.get(), -1, std::numeric_limits<int64_t>::min(), startUs,
							  std::numeric_limits<int64_t>::max(), AVSEEK_FLAG_BACKWARD);
		if (seekResult < 0) {
			return fail(QStringLiteral("Unable to seek export start: %1").arg(ffmpegError(seekResult)));
		}
	}

	const int64_t endUs = request.endMs >= 0 ? av_rescale_q(request.endMs, AVRational{1, 1000}, AV_TIME_BASE_Q)
						 : AV_NOPTS_VALUE;
	const int64_t startUs = request.startMs > 0 ? av_rescale_q(request.startMs, AVRational{1, 1000}, AV_TIME_BASE_Q)
						    : 0;
	std::vector<int64_t> streamOffsets(input->nb_streams, AV_NOPTS_VALUE);
	AVPacket *rawPacket = av_packet_alloc();
	if (!rawPacket) {
		return fail(QStringLiteral("Unable to allocate export packet"));
	}
	PacketPtr packet(rawPacket);

	bool cancelled = false;
	int readResult = 0;
	double lastProgress = 0.0;
	while ((readResult = av_read_frame(input.get(), packet.get())) >= 0) {
		if (shouldCancel && shouldCancel()) {
			cancelled = true;
			av_packet_unref(packet.get());
			break;
		}
		if (packet->stream_index < 0 || packet->stream_index >= static_cast<int>(input->nb_streams)) {
			av_packet_unref(packet.get());
			continue;
		}

		const AVStream *inputStream = input->streams[packet->stream_index];
		const int64_t timestampUs = packetTimestampUs(*packet, *inputStream);

		AudioEncodeStream *audio = nullptr;
		for (AudioEncodeStream &candidate : audioStreams) {
			if (candidate.inputStreamIndex == packet->stream_index) {
				audio = &candidate;
				break;
			}
		}

		if (audio) {
			bool endReached = false;
			const int audioResult =
				audio->pushPacket(packet.get(), output.context, inputStream, endUs, gain, endReached);
			av_packet_unref(packet.get());
			if (audioResult < 0) {
				return fail(QStringLiteral("Unable to encode audio: %1").arg(ffmpegError(audioResult)));
			}
			if (!audio->finished && timestampUs != AV_NOPTS_VALUE && request.progress &&
			    input->duration > 0) {
				const double fraction =
					std::clamp(timestampUs / static_cast<double>(input->duration), 0.0, 1.0);
				if (fraction > lastProgress + 0.02) {
					lastProgress = fraction;
					request.progress(fraction);
				}
			}
			continue;
		}

		if (endUs != AV_NOPTS_VALUE && timestampUs != AV_NOPTS_VALUE && timestampUs >= endUs) {
			av_packet_unref(packet.get());
			break;
		}

		AVStream *outputStream = outputStreams[packet->stream_index];
		const int64_t referenceTimestamp = packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts;
		if (streamOffsets[packet->stream_index] == AV_NOPTS_VALUE && referenceTimestamp != AV_NOPTS_VALUE) {
			streamOffsets[packet->stream_index] =
				av_rescale_q(referenceTimestamp, inputStream->time_base, outputStream->time_base);
		}

		av_packet_rescale_ts(packet.get(), inputStream->time_base, outputStream->time_base);
		const int64_t offset = streamOffsets[packet->stream_index];
		if (offset != AV_NOPTS_VALUE) {
			if (packet->pts != AV_NOPTS_VALUE) {
				packet->pts = std::max<int64_t>(0, packet->pts - offset);
			}
			if (packet->dts != AV_NOPTS_VALUE) {
				packet->dts = std::max<int64_t>(0, packet->dts - offset);
			}
		}

		packet->stream_index = outputStream->index;
		const int writeResult = av_interleaved_write_frame(output.context, packet.get());
		av_packet_unref(packet.get());
		if (writeResult < 0) {
			return fail(QStringLiteral("Unable to write export packet: %1").arg(ffmpegError(writeResult)));
		}

		if (request.progress) {
			const double currentUs = static_cast<double>(timestampUs);
			double fraction = 0.0;
			if (endUs != AV_NOPTS_VALUE) {
				const double span = static_cast<double>(std::max<int64_t>(1, endUs - startUs));
				fraction = std::clamp((currentUs - static_cast<double>(startUs)) / span, 0.0, 1.0);
			} else if (input->duration > 0) {
				fraction = std::clamp(currentUs / static_cast<double>(input->duration), 0.0, 1.0);
			}
			if (fraction > lastProgress + 0.02) {
				lastProgress = fraction;
				request.progress(fraction);
			}
		}
	}

	if (cancelled) {
		return fail(QStringLiteral("Export cancelled"), true);
	}
	if (readResult < 0 && readResult != AVERROR_EOF) {
		return fail(QStringLiteral("Unable to read source packet: %1").arg(ffmpegError(readResult)));
	}

	for (AudioEncodeStream &audio : audioStreams) {
		const AVStream *inputStream = input->streams[audio.inputStreamIndex];
		const int drainResult = audio.drain(output.context, inputStream, gain);
		if (drainResult < 0) {
			return fail(QStringLiteral("Unable to finish audio: %1").arg(ffmpegError(drainResult)));
		}
	}

	output.close();
	if (!QFileInfo::exists(partPath) || QFileInfo(partPath).size() <= 0) {
		return fail(QStringLiteral("Export produced an empty file"));
	}

	QString verificationError;
	if (!verifyOutput(partPath, result.durationMs, verificationError)) {
		return fail(verificationError);
	}

	const qint64 sourceDurationMs = mediaDurationMs(*input);
	const qint64 expectedRangeMs =
		exportmath::expectedDurationMs(request.startMs, request.endMs, sourceDurationMs);
	if (!exportmath::durationMatches(result.durationMs, expectedRangeMs, kTrimStartToleranceMs,
					 kTrimEndToleranceMs)) {
		return fail(QStringLiteral("Exported duration (%1 ms) does not match the selected range (%2 ms)")
				    .arg(result.durationMs)
				    .arg(expectedRangeMs));
	}

	if (!QFile::rename(partPath, request.destinationPath)) {
		return fail(QStringLiteral("Unable to atomically rename export into place"));
	}

	result.succeeded = true;
	return result;
}

} // namespace MoonLit
