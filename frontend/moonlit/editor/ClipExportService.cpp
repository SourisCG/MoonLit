#include "ClipExportService.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

namespace MoonLit {
namespace {

struct FormatContextDeleter {
	void operator()(AVFormatContext *context) const noexcept
	{
		if (context) {
			avformat_close_input(&context);
		}
	}
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;

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
	if (request.startMs < 0 || (request.endMs >= 0 && request.endMs <= request.startMs)) {
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
	for (unsigned int index = 0; index < input->nb_streams; ++index) {
		const AVStream *inputStream = input->streams[index];
		AVStream *outputStream = avformat_new_stream(output.context, nullptr);
		if (!outputStream) {
			return fail(QStringLiteral("Unable to allocate export stream"));
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
	AVPacket *packet = av_packet_alloc();
	if (!packet) {
		return fail(QStringLiteral("Unable to allocate export packet"));
	}

	bool cancelled = false;
	int readResult = 0;
	double lastProgress = 0.0;
	while ((readResult = av_read_frame(input.get(), packet)) >= 0) {
		if (shouldCancel && shouldCancel()) {
			cancelled = true;
			av_packet_unref(packet);
			break;
		}
		if (packet->stream_index < 0 || packet->stream_index >= static_cast<int>(input->nb_streams)) {
			av_packet_unref(packet);
			continue;
		}

		const AVStream *inputStream = input->streams[packet->stream_index];
		const int64_t timestampUs = packetTimestampUs(*packet, *inputStream);
		if (endUs != AV_NOPTS_VALUE && timestampUs != AV_NOPTS_VALUE && timestampUs >= endUs) {
			av_packet_unref(packet);
			break;
		}

		AVStream *outputStream = outputStreams[packet->stream_index];
		const int64_t referenceTimestamp = packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts;
		if (streamOffsets[packet->stream_index] == AV_NOPTS_VALUE && referenceTimestamp != AV_NOPTS_VALUE) {
			streamOffsets[packet->stream_index] =
				av_rescale_q(referenceTimestamp, inputStream->time_base, outputStream->time_base);
		}

		av_packet_rescale_ts(packet, inputStream->time_base, outputStream->time_base);
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
		const int writeResult = av_interleaved_write_frame(output.context, packet);
		av_packet_unref(packet);
		if (writeResult < 0) {
			av_packet_free(&packet);
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
	av_packet_free(&packet);

	if (cancelled) {
		return fail(QStringLiteral("Export cancelled"), true);
	}
	if (readResult < 0 && readResult != AVERROR_EOF) {
		return fail(QStringLiteral("Unable to read source packet: %1").arg(ffmpegError(readResult)));
	}

	output.close();
	if (!QFileInfo::exists(partPath) || QFileInfo(partPath).size() <= 0) {
		return fail(QStringLiteral("Export produced an empty file"));
	}

	QString verificationError;
	if (!verifyOutput(partPath, result.durationMs, verificationError)) {
		return fail(verificationError);
	}

	if (!QFile::rename(partPath, request.destinationPath)) {
		return fail(QStringLiteral("Unable to atomically rename export into place"));
	}

	result.succeeded = true;
	return result;
}

} // namespace MoonLit
