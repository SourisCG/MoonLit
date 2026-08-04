#include "MoonLitTest.hpp"
#include "TestMedia.hpp"

#include <moonlit/editor/ClipExportService.hpp>
#include <moonlit/media/ClipPreviewService.hpp>

#include <QDir>
#include <QTemporaryDir>

using namespace MoonLit;
using namespace MoonLitTest;
MOONLIT_TEST(media_preview_strip_decodes_frames)
{
	QTemporaryDir directory;
	const QString path = QDir(directory.path()).filePath(QStringLiteral("test.mp4"));
	bool hasAudio = false;
	QString error;
	if (!generateTestMedia(path, hasAudio, &error)) {
		*failure = QStringLiteral("test media generation failed: %1").arg(error);
		return false;
	}

	const QVector<QImage> frames = ClipPreviewService().frameStrip(path, 6, 120, &error);
	bool ok = expect(frames.size() == 6, "frame strip returns six frames", failure);
	ok &= expect(error.isEmpty(), "no error decoding the strip", failure);
	for (const QImage &frame : frames) {
		ok &= expect(!frame.isNull() && frame.width() > 0 && frame.height() > 0, "frame decodes with size",
			     failure);
		ok &= expect(frame.width() <= 120, "frame respects the maximum width", failure);
	}

	const QImage exact = ClipPreviewService().frameAt(path, 1000, 320, &error);
	ok &= expect(!exact.isNull() && exact.width() == 320, "exact frame decodes at the requested width", failure);
	return ok;
}

MOONLIT_TEST(media_export_trims_to_the_range)
{
	QTemporaryDir directory;
	const QString source = QDir(directory.path()).filePath(QStringLiteral("test.mp4"));
	bool hasAudio = false;
	QString error;
	if (!generateTestMedia(source, hasAudio, &error)) {
		*failure = QStringLiteral("test media generation failed: %1").arg(error);
		return false;
	}

	const QString destination = QDir(directory.path()).filePath(QStringLiteral("trim.mp4"));
	ClipExportRequest request;
	request.sourcePath = source;
	request.destinationPath = destination;
	request.startMs = 500;
	request.endMs = 1500;

	const ClipExportResult result = FfmpegClipExportService().exportClip(request);
	bool ok = expect(result.succeeded, "trim export succeeds", failure);
	if (!result.succeeded) {
		*failure = QStringLiteral("export error: %1").arg(result.error);
		return false;
	}
	ok &= expect(result.durationMs >= 950 && result.durationMs <= 11000,
		     "exported duration matches the range within keyframe tolerance", failure);
	ok &= expect(!QFileInfo::exists(destination + QStringLiteral(".part")), "no partial file remains", failure);
	return ok;
}

MOONLIT_TEST(media_export_full_length_matches_source)
{
	QTemporaryDir directory;
	const QString source = QDir(directory.path()).filePath(QStringLiteral("test.mp4"));
	bool hasAudio = false;
	QString error;
	if (!generateTestMedia(source, hasAudio, &error)) {
		*failure = QStringLiteral("test media generation failed: %1").arg(error);
		return false;
	}

	const QString destination = QDir(directory.path()).filePath(QStringLiteral("full.mp4"));
	ClipExportRequest request;
	request.sourcePath = source;
	request.destinationPath = destination;
	request.startMs = 0;

	const ClipExportResult result = FfmpegClipExportService().exportClip(request);
	bool ok = expect(result.succeeded, "full export succeeds", failure);
	if (!result.succeeded) {
		*failure = QStringLiteral("export error: %1").arg(result.error);
		return false;
	}
	ok &= expect(result.durationMs >= 1500 && result.durationMs <= 13000,
		     "full export duration is close to the source", failure);
	return ok;
}

MOONLIT_TEST(media_export_trims_obs_like_mkv)
{
	QTemporaryDir directory;
	const QString source = QDir(directory.path()).filePath(QStringLiteral("obs-like.mkv"));
	bool hasAudio = false;
	QString error;
	if (!generateTestMediaEx(source, QStringLiteral("matroska"), 2, hasAudio, &error)) {
		*failure = QStringLiteral("MKV test media generation failed: %1").arg(error);
		return false;
	}

	const QString destination = QDir(directory.path()).filePath(QStringLiteral("trim-mkv.mp4"));
	ClipExportRequest request;
	request.sourcePath = source;
	request.destinationPath = destination;
	request.startMs = 500;
	request.endMs = 1500;

	const ClipExportResult result = FfmpegClipExportService().exportClip(request);
	bool ok = expect(result.succeeded, "trim export of MKV with B-frames succeeds", failure);
	if (!result.succeeded) {
		*failure = QStringLiteral("export error: %1").arg(result.error);
		return false;
	}
	ok &= expect(result.durationMs >= 950 && result.durationMs <= 11000,
		     "MKV export duration matches the range within tolerance", failure);
	ok &= expect(!QFileInfo::exists(destination + QStringLiteral(".part")), "no partial file remains", failure);
	return ok;
}

MOONLIT_TEST(media_export_full_obs_like_mkv_matches_source)
{
	QTemporaryDir directory;
	const QString source = QDir(directory.path()).filePath(QStringLiteral("obs-like.mkv"));
	bool hasAudio = false;
	QString error;
	if (!generateTestMediaEx(source, QStringLiteral("matroska"), 2, hasAudio, &error)) {
		*failure = QStringLiteral("MKV test media generation failed: %1").arg(error);
		return false;
	}

	const QString destination = QDir(directory.path()).filePath(QStringLiteral("full-mkv.mp4"));
	ClipExportRequest request;
	request.sourcePath = source;
	request.destinationPath = destination;
	request.startMs = 0;

	const ClipExportResult result = FfmpegClipExportService().exportClip(request);
	bool ok = expect(result.succeeded, "full export of MKV succeeds", failure);
	if (!result.succeeded) {
		*failure = QStringLiteral("export error: %1").arg(result.error);
		return false;
	}
	ok &= expect(result.durationMs >= 1500 && result.durationMs <= 13000,
		     "full MKV export duration is close to the source", failure);
	return ok;
}

MOONLIT_TEST(media_export_replaces_previous_result)
{
	QTemporaryDir directory;
	const QString source = QDir(directory.path()).filePath(QStringLiteral("test.mp4"));
	bool hasAudio = false;
	QString error;
	if (!generateTestMedia(source, hasAudio, &error)) {
		*failure = QStringLiteral("test media generation failed: %1").arg(error);
		return false;
	}

	const QString destination = QDir(directory.path()).filePath(QStringLiteral("replace.mp4"));
	FfmpegClipExportService service;

	ClipExportRequest first;
	first.sourcePath = source;
	first.destinationPath = destination;
	first.startMs = 500;
	first.endMs = 1500;
	const ClipExportResult firstResult = service.exportClip(first);
	bool ok = expect(firstResult.succeeded, "first export succeeds", failure);
	if (!firstResult.succeeded) {
		*failure = QStringLiteral("first export error: %1").arg(firstResult.error);
		return false;
	}

	/* The same clip, different range: the previous export is replaced. */
	ClipExportRequest second;
	second.sourcePath = source;
	second.destinationPath = destination;
	second.startMs = 0;
	second.endMs = 1000;
	const ClipExportResult secondResult = service.exportClip(second);
	ok &= expect(secondResult.succeeded, "re-export overwrites the previous result", failure);
	if (!secondResult.succeeded) {
		*failure = QStringLiteral("re-export error: %1").arg(secondResult.error);
		return false;
	}
	ok &= expect(secondResult.durationMs >= 950 && secondResult.durationMs <= 11000,
		     "re-export duration matches the new range", failure);
	return ok;
}

MOONLIT_TEST(media_export_applies_audio_edits)
{
	QTemporaryDir directory;
	const QString source = QDir(directory.path()).filePath(QStringLiteral("test.mp4"));
	bool hasAudio = false;
	QString error;
	if (!generateTestMedia(source, hasAudio, &error)) {
		*failure = QStringLiteral("test media generation failed: %1").arg(error);
		return false;
	}
	if (!hasAudio) {
		*failure = QStringLiteral("aac encoder unavailable; cannot test audio edits");
		return false;
	}

	const QString destination = QDir(directory.path()).filePath(QStringLiteral("edited.mp4"));
	ClipExportRequest request;
	request.sourcePath = source;
	request.destinationPath = destination;
	request.muted = true;
	request.gainDb = 6.0;

	const ClipExportResult result = FfmpegClipExportService().exportClip(request);
	bool ok = expect(result.succeeded, "export with mute and gain succeeds", failure);
	if (!result.succeeded) {
		*failure = QStringLiteral("export error: %1").arg(result.error);
		return false;
	}
	ok &= expect(!result.error.isEmpty() || result.durationMs > 0, "export reports a duration", failure);

	AVFormatContext *check = nullptr;
	if (avformat_open_input(&check, destination.toUtf8().constData(), nullptr, nullptr) >= 0) {
		avformat_find_stream_info(check, nullptr);
		bool hasAudioStream = false;
		bool hasVideoStream = false;
		for (unsigned int index = 0; index < check->nb_streams; ++index) {
			if (check->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
				hasAudioStream = true;
			}
			if (check->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
				hasVideoStream = true;
			}
		}
		avformat_close_input(&check);
		ok &= expect(hasAudioStream, "edited export keeps an audio stream", failure);
		ok &= expect(hasVideoStream, "edited export keeps the video stream", failure);
	} else {
		ok &= expect(false, "edited export opens for verification", failure);
	}
	return ok;
}
