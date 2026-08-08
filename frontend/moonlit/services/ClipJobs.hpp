#pragma once

#include <QObject>
#include <QString>

#include <QImage>

#include <moonlit/Clip.hpp>
#include <moonlit/editor/Timeline.hpp>
#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/media/ClipPreviewService.hpp>
#include <moonlit/media/MediaProbe.hpp>
#include <moonlit/media/ThumbnailService.hpp>
#include <moonlit/persistence/SqliteClipRepository.hpp>

namespace MoonLit {

/* Owns the slow media work (probe, thumbnail, ingest, reconcile, search,
 * preview frames, timelines) and shares the repository with the export
 * queue. Lives on a dedicated thread so the UI thread never blocks; every
 * slot runs single-flight because the queue is serial. The repository is
 * opened once by its owner before the worker starts. */
class ClipJobs final : public QObject {
	Q_OBJECT

public:
	explicit ClipJobs(MoonLitPaths paths, SqliteClipRepository *repository, QObject *parent = nullptr);
	~ClipJobs() override;

	ClipJobs(const ClipJobs &) = delete;
	ClipJobs &operator=(const ClipJobs &) = delete;

public slots:
	void reload();
	void loadRecent(int limit);
	void ingest(const QString &path);
	void removeClip(const QString &id);
	void search(const QString &query);
	void saveEdits(const QString &id, qint64 startMs, qint64 endMs, bool muted, double gainDb);
	void previewFrameAt(const QString &path, qint64 positionMs);
	void previewStrip(const QString &path, int count);
	void saveTimeline(const TimelineProject &project);
	void listTimelines();
	void deleteTimeline(const QString &id);
	void loadTimeline(const QString &id);

signals:
	void libraryLoaded(QVector<Clip> clips, const QString &error);
	void recentLoaded(QVector<Clip> clips, const QString &error);
	void clipIngested(const QString &id, const QString &error);
	void clipRemoved(const QString &id, const QString &error);
	void clipEditsSaved(const QString &id, const QString &error);
	void searchResults(QVector<Clip> clips, const QString &query);
	void previewFrameReady(const QString &path, qint64 positionMs, const QImage &image, const QString &error);
	void previewStripReady(const QString &path, const QVector<QImage> &images, const QString &error);
	void timelineSaved(const QString &id, const QString &error);
	void timelinesLoaded(const QVector<TimelineProject> &projects, const QString &error);
	void timelineDeleted(const QString &id, const QString &error);
	void timelineLoaded(const TimelineProject &project, const QString &error);

private:
	MoonLitPaths paths_;
	SqliteClipRepository *repository_ = nullptr;
	FfmpegMediaProbe probe_;
	FfmpegThumbnailService thumbnails_;
	ClipPreviewService preview_;
};

} // namespace MoonLit
