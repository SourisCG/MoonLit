#include "ClipJobs.hpp"

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
	request.progress = [this](double fraction) { emit exportProgress(fraction); };

	const ClipExportResult result = exporter_.exportClip(request, [this] { return cancelExport_.load(); });
	if (result.succeeded || result.cancelled) {
		emit exportProgress(1.0);
	}
	emit exportFinished(result.succeeded, result.cancelled, result.outputPath, result.error);
}

} // namespace MoonLit
