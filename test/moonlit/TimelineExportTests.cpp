#include "MoonLitTest.hpp"
#include "TestMedia.hpp"

#include <moonlit/editor/FfmpegTimelineExporter.hpp>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>

using namespace MoonLit;
using namespace MoonLitTest;

namespace {

TimelineSegmentSource fullClip(const QString &path)
{
	TimelineSegmentSource source;
	source.mediaPath = path;
	source.sourceStartMs = 0;
	source.sourceEndMs = 2000;
	return source;
}

TimelineSegmentSource trimmedClip(const QString &path, qint64 startMs, qint64 endMs)
{
	TimelineSegmentSource source;
	source.mediaPath = path;
	source.sourceStartMs = startMs;
	source.sourceEndMs = endMs;
	return source;
}

} // namespace

MOONLIT_TEST(timeline_export_concatenates_two_segments)
{
	QTemporaryDir directory;
	QString error;
	bool hasAudio = false;
	const QString firstPath = QDir(directory.path()).filePath(QStringLiteral("first.mp4"));
	const QString secondPath = QDir(directory.path()).filePath(QStringLiteral("second.mp4"));
	if (!generateTestMedia(firstPath, hasAudio, &error) || !generateTestMedia(secondPath, hasAudio, &error)) {
		*failure = QStringLiteral("test media generation failed: %1").arg(error);
		return false;
	}

	const QString destination = QDir(directory.path()).filePath(QStringLiteral("timeline.mp4"));
	TimelineExportRequest request;
	request.destinationPath = destination;
	request.segments.append(fullClip(firstPath));
	request.segments.append(fullClip(secondPath));

	const TimelineExportResult result = FfmpegTimelineExporter().exportTimeline(request, nullptr);
	bool ok = expect(result.succeeded, "timeline export succeeds", failure);
	if (!result.succeeded) {
		*failure = QStringLiteral("timeline export error: %1").arg(result.error);
		return false;
	}
	ok &= expect(result.durationMs >= 3800 && result.durationMs <= 5000,
		     "concatenated duration is the sum of both segments", failure);
	ok &= expect(QFileInfo::exists(destination), "output exists", failure);
	ok &= expect(!QFileInfo::exists(destination + QStringLiteral(".part")), "no partial file remains", failure);
	return ok;
}

MOONLIT_TEST(timeline_export_trims_and_mutes_segments)
{
	QTemporaryDir directory;
	QString error;
	bool hasAudio = false;
	const QString firstPath = QDir(directory.path()).filePath(QStringLiteral("first.mp4"));
	const QString secondPath = QDir(directory.path()).filePath(QStringLiteral("second.mp4"));
	if (!generateTestMedia(firstPath, hasAudio, &error) || !generateTestMedia(secondPath, hasAudio, &error)) {
		*failure = QStringLiteral("test media generation failed: %1").arg(error);
		return false;
	}

	const QString destination = QDir(directory.path()).filePath(QStringLiteral("timeline.mp4"));
	TimelineExportRequest request;
	request.destinationPath = destination;
	/* First segment: only the middle second; second: full clip but muted. */
	request.segments.append(trimmedClip(firstPath, 500, 1500));
	request.segments.append(fullClip(secondPath));
	request.segments[1].muted = true;

	const TimelineExportResult result = FfmpegTimelineExporter().exportTimeline(request, nullptr);
	bool ok = expect(result.succeeded, "trimmed and muted export succeeds", failure);
	if (!result.succeeded) {
		*failure = QStringLiteral("timeline export error: %1").arg(result.error);
		return false;
	}
	ok &= expect(result.durationMs >= 2800 && result.durationMs <= 4000,
		     "trimmed segment shortens the total", failure);
	return ok;
}

MOONLIT_TEST(timeline_export_cancels_cleanly)
{
	QTemporaryDir directory;
	QString error;
	bool hasAudio = false;
	const QString firstPath = QDir(directory.path()).filePath(QStringLiteral("first.mp4"));
	const QString secondPath = QDir(directory.path()).filePath(QStringLiteral("second.mp4"));
	if (!generateTestMedia(firstPath, hasAudio, &error) || !generateTestMedia(secondPath, hasAudio, &error)) {
		*failure = QStringLiteral("test media generation failed: %1").arg(error);
		return false;
	}

	const QString destination = QDir(directory.path()).filePath(QStringLiteral("timeline.mp4"));
	TimelineExportRequest request;
	request.destinationPath = destination;
	request.segments.append(fullClip(firstPath));
	request.segments.append(fullClip(secondPath));

	std::atomic_bool cancelled{true};
	const TimelineExportResult result = FfmpegTimelineExporter().exportTimeline(
		request, [&cancelled]() { return cancelled.load(); });
	bool ok = expect(!result.succeeded, "cancel request fails the export", failure);
	ok &= expect(!QFileInfo::exists(destination), "cancelled export leaves no final file", failure);
	ok &= expect(!QFileInfo::exists(destination + QStringLiteral(".part")), "cancelled export leaves no partial", failure);
	return ok;
}

MOONLIT_TEST(timeline_export_rejects_missing_sources)
{
	QTemporaryDir directory;
	const QString destination = QDir(directory.path()).filePath(QStringLiteral("timeline.mp4"));
	TimelineExportRequest request;
	request.destinationPath = destination;
	request.segments.append(fullClip(QDir(directory.path()).filePath(QStringLiteral("missing.mp4"))));

	const TimelineExportResult result = FfmpegTimelineExporter().exportTimeline(request, nullptr);
	bool ok = expect(!result.succeeded, "missing source fails the export", failure);
	ok &= expect(result.error.contains(QStringLiteral("does not exist")), "error names the missing file", failure);
	return ok;
}
