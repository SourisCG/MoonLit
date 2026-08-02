#include "MediaProbe.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}

#include <QFile>
#include <QFileInfo>

#include <memory>

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

QString ffmpegError(int code)
{
	char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
	if (av_strerror(code, buffer, sizeof(buffer)) < 0) {
		return QStringLiteral("FFmpeg error %1").arg(code);
	}

	return QString::fromUtf8(buffer);
}

void setError(QString *error, const QString &message)
{
	if (error) {
		*error = message;
	}
}

QString codecName(AVCodecID codecId)
{
	return QString::fromUtf8(avcodec_get_name(codecId));
}

qint64 durationMs(int64_t duration, AVRational timeBase)
{
	if (duration == AV_NOPTS_VALUE || timeBase.num <= 0 || timeBase.den <= 0) {
		return -1;
	}

	return av_rescale_q(duration, timeBase, AVRational{1, 1000});
}

} // namespace

std::optional<ClipMetadata> FfmpegMediaProbe::probe(const QString &mediaPath, QString *error) const
{
	const QFileInfo fileInfo(mediaPath);
	if (!fileInfo.exists() || !fileInfo.isFile()) {
		setError(error, QStringLiteral("Media file does not exist: %1").arg(mediaPath));
		return std::nullopt;
	}

	AVFormatContext *rawContext = nullptr;
	const QByteArray encodedPath = QFile::encodeName(fileInfo.absoluteFilePath());
	const int openResult = avformat_open_input(&rawContext, encodedPath.constData(), nullptr, nullptr);
	if (openResult < 0) {
		setError(error, QStringLiteral("Unable to open media file: %1").arg(ffmpegError(openResult)));
		return std::nullopt;
	}

	FormatContextPtr context(rawContext);
	const int streamInfoResult = avformat_find_stream_info(context.get(), nullptr);
	if (streamInfoResult < 0) {
		setError(error, QStringLiteral("Unable to read media streams: %1").arg(ffmpegError(streamInfoResult)));
		return std::nullopt;
	}

	ClipMetadata metadata;
	if (context->iformat) {
		metadata.container = QString::fromUtf8(context->iformat->name);
	}
	metadata.durationMs = durationMs(context->duration, AV_TIME_BASE_Q);
	metadata.bitRate = context->bit_rate > 0 ? context->bit_rate : 0;

	for (unsigned int index = 0; index < context->nb_streams; ++index) {
		AVStream *stream = context->streams[index];
		const AVCodecParameters *parameters = stream->codecpar;
		if (!parameters) {
			continue;
		}

		if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
			++metadata.videoStreamCount;
			if (metadata.videoCodec.isEmpty()) {
				metadata.width = parameters->width;
				metadata.height = parameters->height;
				const AVRational frameRate = av_guess_frame_rate(context.get(), stream, nullptr);
				if (frameRate.num > 0 && frameRate.den > 0) {
					metadata.frameRate = av_q2d(frameRate);
				}
				metadata.videoCodec = codecName(parameters->codec_id);
			}
			if (metadata.durationMs < 0) {
				metadata.durationMs = durationMs(stream->duration, stream->time_base);
			}
		} else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
			++metadata.audioStreamCount;
			metadata.hasAudio = true;
			if (metadata.audioCodec.isEmpty()) {
				metadata.audioCodec = codecName(parameters->codec_id);
			}
		}
	}

	return metadata;
}

} // namespace MoonLit
