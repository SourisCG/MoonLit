#include "ThumbnailService.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <cerrno>
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

std::optional<QImage> convertFrame(const AVFrame &frame, const QSize &maximumSize, QString *error)
{
	if (frame.width <= 0 || frame.height <= 0 || frame.format == AV_PIX_FMT_NONE) {
		setError(error, QStringLiteral("Decoded frame has invalid dimensions"));
		return std::nullopt;
	}

	QSize targetSize(frame.width, frame.height);
	if (maximumSize.isValid() && !maximumSize.isEmpty()) {
		targetSize = targetSize.scaled(maximumSize, Qt::KeepAspectRatio);
	}

	QImage image(targetSize, QImage::Format_RGBA8888);
	if (image.isNull()) {
		setError(error, QStringLiteral("Unable to allocate thumbnail image"));
		return std::nullopt;
	}

	SwsContextPtr scaler(sws_getContext(frame.width, frame.height, static_cast<AVPixelFormat>(frame.format),
					    targetSize.width(), targetSize.height(), AV_PIX_FMT_RGBA, SWS_BILINEAR,
					    nullptr, nullptr, nullptr));
	if (!scaler) {
		setError(error, QStringLiteral("Unable to create thumbnail scaler"));
		return std::nullopt;
	}

	uint8_t *destinationData[4] = {image.bits(), nullptr, nullptr, nullptr};
	int destinationLinesize[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
	const int scaledLines = sws_scale(scaler.get(), frame.data, frame.linesize, 0, frame.height, destinationData,
					  destinationLinesize);
	if (scaledLines <= 0) {
		setError(error, QStringLiteral("Unable to scale thumbnail frame"));
		return std::nullopt;
	}

	return image;
}

std::optional<QImage> receiveFrame(AVCodecContext *codec, AVFrame *frame, const QSize &maximumSize, QString *error)
{
	while (true) {
		const int receiveResult = avcodec_receive_frame(codec, frame);
		if (receiveResult == 0) {
			return convertFrame(*frame, maximumSize, error);
		}
		if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
			return std::nullopt;
		}

		setError(error, QStringLiteral("Unable to decode thumbnail frame: %1").arg(ffmpegError(receiveResult)));
		return std::nullopt;
	}
}

} // namespace

std::optional<QImage> FfmpegThumbnailService::frameAt(const QString &mediaPath, const ThumbnailOptions &options,
						      QString *error) const
{
	if (options.timestampMs < 0) {
		setError(error, QStringLiteral("Thumbnail timestamp cannot be negative"));
		return std::nullopt;
	}

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

	const AVCodec *decoder = nullptr;
	const int streamIndex = av_find_best_stream(context.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
	if (streamIndex < 0 || !decoder) {
		setError(error, QStringLiteral("Media file has no decodable video stream"));
		return std::nullopt;
	}

	CodecContextPtr codec(avcodec_alloc_context3(decoder));
	if (!codec) {
		setError(error, QStringLiteral("Unable to allocate video decoder"));
		return std::nullopt;
	}
	if (avcodec_parameters_to_context(codec.get(), context->streams[streamIndex]->codecpar) < 0) {
		setError(error, QStringLiteral("Unable to configure video decoder"));
		return std::nullopt;
	}
	const int codecOpenResult = avcodec_open2(codec.get(), decoder, nullptr);
	if (codecOpenResult < 0) {
		setError(error, QStringLiteral("Unable to open video decoder: %1").arg(ffmpegError(codecOpenResult)));
		return std::nullopt;
	}

	if (options.timestampMs > 0) {
		const AVStream *stream = context->streams[streamIndex];
		const int64_t timestamp = av_rescale_q(options.timestampMs, AVRational{1, 1000}, stream->time_base);
		const int seekResult = av_seek_frame(context.get(), streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
		if (seekResult < 0) {
			setError(error,
				 QStringLiteral("Unable to seek thumbnail frame: %1").arg(ffmpegError(seekResult)));
			return std::nullopt;
		}
		avcodec_flush_buffers(codec.get());
	}

	FramePtr frame(av_frame_alloc());
	PacketPtr packet(av_packet_alloc());
	if (!frame || !packet) {
		setError(error, QStringLiteral("Unable to allocate thumbnail decoder buffers"));
		return std::nullopt;
	}

	while (av_read_frame(context.get(), packet.get()) >= 0) {
		if (packet->stream_index != streamIndex) {
			av_packet_unref(packet.get());
			continue;
		}

		const int sendResult = avcodec_send_packet(codec.get(), packet.get());
		av_packet_unref(packet.get());
		if (sendResult < 0) {
			setError(error,
				 QStringLiteral("Unable to send thumbnail packet: %1").arg(ffmpegError(sendResult)));
			return std::nullopt;
		}

		QString receiveError;
		const std::optional<QImage> image =
			receiveFrame(codec.get(), frame.get(), options.maximumSize, &receiveError);
		if (image) {
			return image;
		}
		if (!receiveError.isEmpty()) {
			setError(error, receiveError);
			return std::nullopt;
		}
	}

	const int flushResult = avcodec_send_packet(codec.get(), nullptr);
	if (flushResult < 0 && flushResult != AVERROR_EOF) {
		setError(error, QStringLiteral("Unable to flush thumbnail decoder: %1").arg(ffmpegError(flushResult)));
		return std::nullopt;
	}

	QString receiveError;
	const std::optional<QImage> image = receiveFrame(codec.get(), frame.get(), options.maximumSize, &receiveError);
	if (image) {
		return image;
	}
	setError(error, receiveError.isEmpty() ? QStringLiteral("No video frame was decoded") : receiveError);
	return std::nullopt;
}

bool FfmpegThumbnailService::writeThumbnail(const QString &mediaPath, const QString &thumbnailPath,
					    const ThumbnailOptions &options, QString *error) const
{
	const std::optional<QImage> image = frameAt(mediaPath, options, error);
	if (!image) {
		return false;
	}

	const QFileInfo fileInfo(thumbnailPath);
	if (!QDir().mkpath(fileInfo.absolutePath())) {
		setError(error,
			 QStringLiteral("Unable to create thumbnail directory: %1").arg(fileInfo.absolutePath()));
		return false;
	}

	QSaveFile file(thumbnailPath);
	if (!file.open(QIODevice::WriteOnly)) {
		setError(error, QStringLiteral("Unable to write thumbnail: %1").arg(file.errorString()));
		return false;
	}
	if (!image->save(&file, "PNG") || !file.commit()) {
		setError(error, QStringLiteral("Unable to commit thumbnail: %1").arg(file.errorString()));
		return false;
	}

	return true;
}

} // namespace MoonLit
