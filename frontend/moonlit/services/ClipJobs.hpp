#pragma once

#include <QObject>
#include <QString>

#include <QImage>

#include <atomic>

#include <moonlit/Clip.hpp>
#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/editor/ClipExportService.hpp>
#include <moonlit/media/ClipPreviewService.hpp>
#include <moonlit/media/MediaProbe.hpp>
#include <moonlit/media/ThumbnailService.hpp>
#include <moonlit/persistence/SqliteClipRepository.hpp>

namespace MoonLit {

/* Owns the repository and all slow media work (probe, thumbnail, ingest,
 * reconcile, search, export, preview frames). Lives on a dedicated thread so
 * the UI thread never blocks; every slot runs single-flight because the queue
 * is serial. */
class ClipJobs final : public QObject {
	Q_OBJECT

public:
	explicit ClipJobs(MoonLitPaths paths, QObject *parent = nullptr);
	~ClipJobs() override;

	ClipJobs(const ClipJobs &) = delete;
	ClipJobs &operator=(const ClipJobs &) = delete;

	/* Thread-safe: may be called from any thread, never queued. */
	void cancelExport() { cancelExport_.store(true); }

public slots:
	void reload();
	void ingest(const QString &path);
	void removeClip(const QString &id);
	void search(const QString &query);
	void exportClip(const QString &id, qint64 startMs, qint64 endMs);
	void saveEdits(const QString &id, qint64 startMs, qint64 endMs, bool muted, double gainDb);
	void previewFrameAt(const QString &path, qint64 positionMs);
	void previewStrip(const QString &path, int count);

signals:
	void libraryLoaded(QVector<Clip> clips, const QString &error);
	void clipIngested(const QString &id, const QString &error);
	void clipRemoved(const QString &id, const QString &error);
	void clipEditsSaved(const QString &id, const QString &error);
	void searchResults(QVector<Clip> clips, const QString &query);
	void exportProgress(double fraction);
	void exportFinished(bool succeeded, bool cancelled, const QString &outputPath, const QString &error);
	void previewFrameReady(const QString &path, qint64 positionMs, const QImage &image, const QString &error);
	void previewStripReady(const QString &path, const QVector<QImage> &images, const QString &error);

private:
	MoonLitPaths paths_;
	SqliteClipRepository repository_;
	FfmpegMediaProbe probe_;
	FfmpegThumbnailService thumbnails_;
	ClipPreviewService preview_;
	FfmpegClipExportService exporter_;
	std::atomic_bool cancelExport_{false};
};

} // namespace MoonLit
