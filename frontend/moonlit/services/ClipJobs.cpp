#include "ClipJobs.hpp"

#include <algorithm>

namespace MoonLit {

ClipJobs::ClipJobs(MoonLitPaths paths, QObject *parent) : QObject(parent), paths_(std::move(paths)), repository_(paths_)
{
}

ClipJobs::~ClipJobs() = default;

void ClipJobs::reload()
{
	QString error;
	if (!repository_.open(&error)) {
		emit libraryLoaded({}, error);
		return;
	}
	repository_.reconcile(nullptr, &error);
	emit libraryLoaded(repository_.list(true), error);
}

void ClipJobs::ingest(const QString &path)
{
	QString error;
	if (path.isEmpty()) {
		emit clipIngested(QString(), QStringLiteral("Ruta vacia"));
		return;
	}
	if (!repository_.open(&error)) {
		emit clipIngested(QString(), error);
		return;
	}
	if (repository_.findByMediaPath(path)) {
		emit clipIngested(QString(), QString());
		return;
	}

	Clip clip = Clip::create(path);
	if (const auto metadata = probe_.probe(path, &error)) {
		clip.metadata = *metadata;
	}
	clip.thumbnailPath = paths_.thumbnailPath(clip.id);

	const auto stored = repository_.upsert(clip, &error);
	if (!stored) {
		emit clipIngested(QString(), error);
		return;
	}

	QString thumbnailError;
	const qint64 timestamp = stored->metadata.durationMs > 0 ? stored->metadata.durationMs / 4 : 0;
	ThumbnailOptions options;
	options.timestampMs = timestamp;
	if (!thumbnails_.writeThumbnail(stored->mediaPath, stored->thumbnailPath, options, &thumbnailError)) {
		emit clipIngested(stored->id,
				  QStringLiteral("Clip guardado; thumbnail pendiente: %1").arg(thumbnailError));
		return;
	}
	emit clipIngested(stored->id, QString());
}

void ClipJobs::removeClip(const QString &id)
{
	QString error;
	if (!repository_.open(&error)) {
		emit clipRemoved(id, error);
		return;
	}
	if (!repository_.remove(id, &error)) {
		emit clipRemoved(id, error);
		return;
	}
	emit clipRemoved(id, QString());
}

void ClipJobs::search(const QString &query)
{
	QString error;
	if (!repository_.open(&error)) {
		emit searchResults({}, query);
		return;
	}
	repository_.reconcile(nullptr, &error);
	emit searchResults(repository_.search(query), query);
}

void ClipJobs::saveEdits(const QString &id, qint64 startMs, qint64 endMs, bool muted, double gainDb)
{
	QString error;
	if (!repository_.open(&error)) {
		emit clipEditsSaved(id, error);
		return;
	}
	const auto clip = repository_.find(id);
	if (!clip) {
		emit clipEditsSaved(id, QStringLiteral("Clip no encontrado"));
		return;
	}

	Clip edited = *clip;
	edited.trimStartMs = std::max<qint64>(0, startMs);
	edited.trimEndMs = endMs > 0 ? std::max(endMs, edited.trimStartMs + 1) : -1;
	edited.muted = muted;
	edited.gainDb = gainDb;

	if (!repository_.upsert(edited, &error)) {
		emit clipEditsSaved(id, error);
		return;
	}
	emit clipEditsSaved(id, QString());
}

void ClipJobs::previewFrameAt(const QString &path, qint64 positionMs)
{
	QString error;
	const QImage image = preview_.frameAt(path, positionMs, 640, &error);
	emit previewFrameReady(path, positionMs, image, error);
}

void ClipJobs::previewStrip(const QString &path, int count)
{
	QString error;
	const QVector<QImage> images = preview_.frameStrip(path, count, 160, &error);
	emit previewStripReady(path, images, error);
}

void ClipJobs::exportClip(const QString &id, qint64 startMs, qint64 endMs)
{
	cancelExport_.store(false);

	QString error;
	if (!repository_.open(&error)) {
		emit exportFinished(false, false, QString(), error);
		return;
	}
	const auto clip = repository_.find(id);
	if (!clip || clip->missing) {
		emit exportFinished(false, false, QString(), QStringLiteral("Clip no disponible"));
		return;
	}
	if (endMs > 0 && endMs <= startMs) {
		emit exportFinished(false, false, QString(), QStringLiteral("El final debe ser mayor que el inicio"));
		return;
	}

	ClipExportRequest request;
	request.sourcePath = clip->mediaPath;
	request.destinationPath = paths_.exportPath(clip->id, QStringLiteral("mp4"));
	request.startMs = startMs;
	request.endMs = endMs;
	request.muted = clip->muted;
	request.gainDb = clip->gainDb;
	request.progress = [this](double fraction) { emit exportProgress(fraction); };

	const ClipExportResult result = exporter_.exportClip(request, [this] { return cancelExport_.load(); });
	if (result.succeeded || result.cancelled) {
		emit exportProgress(1.0);
	}
	emit exportFinished(result.succeeded, result.cancelled, result.outputPath, result.error);
}

} // namespace MoonLit
