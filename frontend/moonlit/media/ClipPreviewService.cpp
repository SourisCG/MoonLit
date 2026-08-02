#include "ClipPreviewService.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <QDir>
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

struct SwsContextDeleter {
	void operator()(SwsContext *context) const noexcept
	{
		if (context) {
			sws_freeContext(context);
		}
	}
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

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

QImage convertFrame(const AVFrame &frame, int maximumWidth)
{
	if (frame.width <= 0 || frame.height <= 0 || frame.format == AV_PIX_FMT_NONE) {
		return QImage();
	}

	QSize targetSize(frame.width, frame.height);
	if (maximumWidth > 0) {
		targetSize.scale(maximumWidth, maximumWidth, Qt::KeepAspectRatio);
	}

	QImage image(targetSize, QImage::Format_RGBA8888);
	if (image.isNull()) {
		return QImage();
	}

	SwsContextPtr scaler(sws_getContext(frame.width, frame.height, static_cast<AVPixelFormat>(frame.format),
					    targetSize.width(), targetSize.height(), AV_PIX_FMT_RGBA, SWS_BILINEAR,
					    nullptr, nullptr, nullptr));
	if (!scaler) {
		return QImage();
	}

	uint8_t *destinationData[4] = {image.bits(), nullptr, nullptr, nullptr};
	int destinationLinesize[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
	const int scaledLines = sws_scale(scaler.get(), frame.data, frame.linesize, 0, frame.height, destinationData,
					  destinationLinesize);
	if (scaledLines <= 0) {
		return QImage();
	}

	return image;
}

/* Decodes the frame closest at or after `targetUs` (AV_TIME_BASE units).
 * The caller must have seeked; falls back to the last decoded frame on EOF. */
QImage decodeAtOrAfter(AVFormatContext *context, int streamIndex, AVCodecContext *codec, int64_t targetUs)
{
	FramePtr frame(av_frame_alloc());
	PacketPtr packet(av_packet_alloc());
	if (!frame || !packet) {
		return QImage();
	}

	QImage lastFrame;
	while (av_read_frame(context, packet.get()) >= 0) {
		if (packet->stream_index != streamIndex) {
			av_packet_unref(packet.get());
			continue;
		}

		const int sendResult = avcodec_send_packet(codec, packet.get());
		av_packet_unref(packet.get());
		if (sendResult < 0) {
			continue;
		}

		while (avcodec_receive_frame(codec, frame.get()) == 0) {
			const int64_t frameUs = frame->best_effort_timestamp != AV_NOPTS_VALUE
							? av_rescale_q(frame->best_effort_timestamp,
								       context->streams[streamIndex]->time_base,
								       AV_TIME_BASE_Q)
							: AV_NOPTS_VALUE;
			const QImage image = convertFrame(*frame, 0);
			if (!image.isNull()) {
				lastFrame = image;
			}
			if (frameUs != AV_NOPTS_VALUE && frameUs >= targetUs) {
				return lastFrame;
			}
		}
	}

	return lastFrame;
}

} // namespace

QImage ClipPreviewService::frameAt(const QString &mediaPath, qint64 positionMs, int maximumWidth,
				   QString *error) const
{
	if (positionMs < 0) {
		setError(error, QStringLiteral("Preview position cannot be negative"));
		return QImage();
	}

	const QFileInfo fileInfo(mediaPath);
	if (!fileInfo.exists() || !fileInfo.isFile()) {
		setError(error, QStringLiteral("Media file does not exist: %1").arg(mediaPath));
		return QImage();
	}

	AVFormatContext *rawContext = nullptr;
	const QByteArray encodedPath = QFile::encodeName(fileInfo.absoluteFilePath());
	const int openResult = avformat_open_input(&rawContext, encodedPath.constData(), nullptr, nullptr);
	if (openResult < 0) {
		setError(error, QStringLiteral("Unable to open media file: %1").arg(ffmpegError(openResult)));
		return QImage();
	}

	FormatContextPtr context(rawContext);
	if (avformat_find_stream_info(context.get(), nullptr) < 0) {
		setError(error, QStringLiteral("Unable to read media streams"));
		return QImage();
	}

	const AVCodec *decoder = nullptr;
	const int streamIndex = av_find_best_stream(context.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
	if (streamIndex < 0 || !decoder) {
		setError(error, QStringLiteral("Media file has no decodable video stream"));
		return QImage();
	}

	CodecContextPtr codec(avcodec_alloc_context3(decoder));
	if (!codec) {
		setError(error, QStringLiteral("Unable to allocate video decoder"));
		return QImage();
	}
	if (avcodec_parameters_to_context(codec.get(), context->streams[streamIndex]->codecpar) < 0) {
		setError(error, QStringLiteral("Unable to configure video decoder"));
		return QImage();
	}
	if (avcodec_open2(codec.get(), decoder, nullptr) < 0) {
		setError(error, QStringLiteral("Unable to open video decoder"));
		return QImage();
	}

	const AVStream *stream = context->streams[streamIndex];
	if (positionMs > 0) {
		const int64_t timestamp = av_rescale_q(positionMs, AVRational{1, 1000}, stream->time_base);
		const int seekResult = av_seek_frame(context.get(), streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
		if (seekResult < 0) {
			setError(error, QStringLiteral("Unable to seek preview frame: %1").arg(ffmpegError(seekResult)));
			return QImage();
		}
		avcodec_flush_buffers(codec.get());
	}

	const int64_t targetUs = av_rescale_q(positionMs, AVRational{1, 1000}, AV_TIME_BASE_Q);
	const QImage image = decodeAtOrAfter(context.get(), streamIndex, codec.get(), targetUs);
	if (image.isNull()) {
		setError(error, QStringLiteral("No video frame was decoded"));
		return QImage();
	}

	QImage scaled = image;
	if (maximumWidth > 0 && scaled.width() > maximumWidth) {
		scaled = scaled.scaledToWidth(maximumWidth, Qt::SmoothTransformation);
	}
	return scaled;
}

QVector<QImage> ClipPreviewService::frameStrip(const QString &mediaPath, int count, int maximumWidth,
					       QString *error) const
{
	QVector<QImage> result;
	if (count <= 0) {
		setError(error, QStringLiteral("Frame strip count must be positive"));
		return result;
	}

	const QFileInfo fileInfo(mediaPath);
	if (!fileInfo.exists() || !fileInfo.isFile()) {
		setError(error, QStringLiteral("Media file does not exist: %1").arg(mediaPath));
		return result;
	}

	AVFormatContext *rawContext = nullptr;
	const QByteArray encodedPath = QFile::encodeName(fileInfo.absoluteFilePath());
	const int openResult = avformat_open_input(&rawContext, encodedPath.constData(), nullptr, nullptr);
	if (openResult < 0) {
		setError(error, QStringLiteral("Unable to open media file: %1").arg(ffmpegError(openResult)));
		return result;
	}

	FormatContextPtr context(rawContext);
	if (avformat_find_stream_info(context.get(), nullptr) < 0) {
		setError(error, QStringLiteral("Unable to read media streams"));
		return result;
	}

	const AVCodec *decoder = nullptr;
	const int streamIndex = av_find_best_stream(context.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
	if (streamIndex < 0 || !decoder) {
		setError(error, QStringLiteral("Media file has no decodable video stream"));
		return result;
	}

	CodecContextPtr codec(avcodec_alloc_context3(decoder));
	if (!codec) {
		setError(error, QStringLiteral("Unable to allocate video decoder"));
		return result;
	}
	if (avcodec_parameters_to_context(codec.get(), context->streams[streamIndex]->codecpar) < 0) {
		setError(error, QStringLiteral("Unable to configure video decoder"));
		return result;
	}
	if (avcodec_open2(codec.get(), decoder, nullptr) < 0) {
		setError(error, QStringLiteral("Unable to open video decoder"));
		return result;
	}

	const AVStream *stream = context->streams[streamIndex];
	const qint64 durationMs = stream->duration != AV_NOPTS_VALUE
				      ? av_rescale_q(stream->duration, stream->time_base, AVRational{1, 1000})
				      : (context->duration != AV_NOPTS_VALUE
						 ? av_rescale_q(context->duration, AV_TIME_BASE_Q, AVRational{1, 1000})
						 : 0);
	if (durationMs <= 0) {
		setError(error, QStringLiteral("Media duration is unknown"));
		return result;
	}

	for (int index = 0; index < count; ++index) {
		const qint64 positionMs = index == 0 ? 0 : (durationMs * index) / (count - 1);
		if (positionMs > 0) {
			const int64_t timestamp = av_rescale_q(positionMs, AVRational{1, 1000}, stream->time_base);
			if (av_seek_frame(context.get(), streamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
				break;
			}
			avcodec_flush_buffers(codec.get());
		}

		const int64_t targetUs = av_rescale_q(positionMs, AVRational{1, 1000}, AV_TIME_BASE_Q);
		QImage image = decodeAtOrAfter(context.get(), streamIndex, codec.get(), targetUs);
		if (image.isNull()) {
			break;
		}
		if (maximumWidth > 0 && image.width() > maximumWidth) {
			image = image.scaledToWidth(maximumWidth, Qt::SmoothTransformation);
		}
		result.append(image);
	}

	if (result.isEmpty()) {
		setError(error, QStringLiteral("No video frames were decoded"));
	}
	return result;
}

} // namespace MoonLit
