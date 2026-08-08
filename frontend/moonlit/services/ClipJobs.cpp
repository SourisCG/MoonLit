#include "ClipJobs.hpp"

#include <algorithm>

namespace MoonLit {

ClipJobs::ClipJobs(MoonLitPaths paths, SqliteClipRepository *repository, QObject *parent)
	: QObject(parent), paths_(std::move(paths)), repository_(repository)
{
}

ClipJobs::~ClipJobs() = default;

void ClipJobs::reload()
{
	if (!repository_) {
		emit libraryLoaded({}, QStringLiteral("Repository not available"));
		return;
	}
	QString error;
	repository_->reconcile(nullptr, &error);
	emit libraryLoaded(repository_->list(true), error);
}

/* Lightweight startup query: only the most recent clips, for the dashboard
 * recents. Never builds the full library grid on the UI thread. */
void ClipJobs::loadRecent(int limit)
{
	if (!repository_) {
		emit recentLoaded({}, QStringLiteral("Repository not available"));
		return;
	}
	QString error;
	QVector<Clip> clips = repository_->list(true);
	std::sort(clips.begin(), clips.end(),
		  [](const Clip &left, const Clip &right) { return left.createdAtUtc > right.createdAtUtc; });
	if (limit > 0 && clips.size() > limit) {
		clips.resize(limit);
	}
	emit recentLoaded(clips, error);
}

void ClipJobs::ingest(const QString &path)
{
	QString error;
	if (path.isEmpty()) {
		emit clipIngested(QString(), QStringLiteral("Ruta vacia"));
		return;
	}
	if (!repository_ || repository_->findByMediaPath(path)) {
		emit clipIngested(QString(), QString());
		return;
	}

	Clip clip = Clip::create(path);
	if (const auto metadata = probe_.probe(path, &error)) {
		clip.metadata = *metadata;
	}
	clip.thumbnailPath = paths_.thumbnailPath(clip.id);

	const auto stored = repository_->upsert(clip, &error);
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
	if (!repository_ || !repository_->remove(id, &error)) {
		emit clipRemoved(id, error);
		return;
	}
	emit clipRemoved(id, QString());
}

void ClipJobs::search(const QString &query)
{
	QString error;
	if (!repository_) {
		emit searchResults({}, query);
		return;
	}
	repository_->reconcile(nullptr, &error);
	emit searchResults(repository_->search(query), query);
}

void ClipJobs::saveEdits(const QString &id, qint64 startMs, qint64 endMs, bool muted, double gainDb)
{
	QString error;
	if (!repository_) {
		emit clipEditsSaved(id, QStringLiteral("Repository not available"));
		return;
	}
	const auto clip = repository_->find(id);
	if (!clip) {
		emit clipEditsSaved(id, QStringLiteral("Clip no encontrado"));
		return;
	}

	Clip edited = *clip;
	edited.trimStartMs = std::max<qint64>(0, startMs);
	edited.trimEndMs = endMs > 0 ? std::max(endMs, edited.trimStartMs + 1) : -1;
	edited.muted = muted;
	edited.gainDb = gainDb;

	if (!repository_->upsert(edited, &error)) {
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

void ClipJobs::saveTimeline(const TimelineProject &project)
{
	QString error;
	if (!repository_) {
		emit timelineSaved(project.id, QStringLiteral("Repository not available"));
		return;
	}
	if (!project.isValid(&error)) {
		emit timelineSaved(project.id, error);
		return;
	}
	if (!repository_->saveTimeline(project, &error)) {
		emit timelineSaved(project.id, error);
		return;
	}
	emit timelineSaved(project.id, QString());
}

void ClipJobs::listTimelines()
{
	QString error;
	if (!repository_) {
		emit timelinesLoaded({}, QStringLiteral("Repository not available"));
		return;
	}
	emit timelinesLoaded(repository_->listTimelines(&error), error);
}

void ClipJobs::deleteTimeline(const QString &id)
{
	QString error;
	if (!repository_ || !repository_->deleteTimeline(id, &error)) {
		emit timelineDeleted(id, error);
		return;
	}
	emit timelineDeleted(id, QString());
}

void ClipJobs::loadTimeline(const QString &id)
{
	QString error;
	if (!repository_) {
		emit timelineLoaded(TimelineProject{}, QStringLiteral("Repository not available"));
		return;
	}
	const auto project = repository_->loadTimeline(id, &error);
	if (!project) {
		emit timelineLoaded(TimelineProject{}, error);
		return;
	}
	emit timelineLoaded(*project, QString());
}

} // namespace MoonLit
