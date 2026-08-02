#include "MoonLitTest.hpp"

#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/persistence/SqliteClipRepository.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace MoonLit;
using namespace MoonLitTest;

namespace {

Clip makeClip(const QString &mediaPath, const QString &title)
{
	Clip clip = Clip::create(mediaPath, title);
	clip.metadata.container = QStringLiteral("mkv");
	clip.metadata.videoCodec = QStringLiteral("h264");
	clip.metadata.durationMs = 30 * 1000;
	clip.metadata.width = 1920;
	clip.metadata.height = 1080;
	clip.metadata.hasAudio = true;
	clip.trimEndMs = clip.metadata.durationMs;
	return clip;
}

bool createDummyFile(const QString &path, QString *failure)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		*failure = QStringLiteral("unable to create dummy file: %1").arg(path);
		return false;
	}
	file.write("x", 1);
	return true;
}

} // namespace

MOONLIT_TEST(sqlite_round_trip)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;
	SqliteClipRepository repository(paths);
	if (!repository.open(&error)) {
		*failure = QStringLiteral("open failed: %1").arg(error);
		return false;
	}

	const QString mediaPath = paths.clipsPath() + QStringLiteral("/session-2026-08-02-0001.mkv");
	if (!createDummyFile(mediaPath, failure)) {
		return false;
	}

	const Clip clip = makeClip(mediaPath, QStringLiteral("Super Mario Kart World"));
	const auto saved = repository.upsert(clip, &error);
	if (!saved) {
		*failure = QStringLiteral("upsert failed: %1").arg(error);
		return false;
	}

	bool ok = expect(!saved->id.isEmpty(), "upsert assigned an id", failure);
	ok &= expect(saved->title == clip.title, "upsert preserved title", failure);
	ok &= expect(!saved->missing, "upsert refreshed file state", failure);

	const QVector<Clip> clips = repository.list();
	ok &= expect(clips.size() == 1, "list has one clip", failure);
	ok &= expect(clips.first().id == saved->id, "list returns the clip", failure);

	const auto byPath = repository.findByMediaPath(mediaPath.toUpper());
	ok &= expect(byPath.has_value() && byPath->id == saved->id, "findByMediaPath is case-insensitive", failure);

	const auto byId = repository.find(saved->id);
	ok &= expect(byId.has_value() && byId->title == clip.title, "find by id returns the clip", failure);

	const QVector<Clip> searchHits = repository.search(QStringLiteral("mario"));
	ok &= expect(searchHits.size() == 1 && searchHits.first().id == saved->id, "FTS search hits title token", failure);

	const QVector<Clip> searchMisses = repository.search(QStringLiteral("zephyria"));
	ok &= expect(searchMisses.isEmpty(), "FTS search misses unrelated term", failure);

	ok &= expect(repository.remove(saved->id, &error), "remove succeeded", failure);
	ok &= expect(repository.list().isEmpty(), "list is empty after remove", failure);
	return ok;
}

MOONLIT_TEST(sqlite_persists_across_reopen)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;
	const QString mediaPath = paths.clipsPath() + QStringLiteral("/session-2026-08-02-0002.mkv");

	{
		SqliteClipRepository repository(paths);
		if (!repository.open(&error)) {
			*failure = QStringLiteral("first open failed: %1").arg(error);
			return false;
		}
		if (!createDummyFile(mediaPath, failure)) {
			return false;
		}
		repository.upsert(makeClip(mediaPath, QStringLiteral("Reopen Me")), &error);
	}

	SqliteClipRepository repository(paths);
	if (!repository.open(&error)) {
		*failure = QStringLiteral("second open failed: %1").arg(error);
		return false;
	}

	bool ok = expect(repository.list().size() == 1, "data survives reopen", failure);
	const auto byPath = repository.findByMediaPath(mediaPath);
	ok &= expect(byPath.has_value() && byPath->title == QStringLiteral("Reopen Me"), "reopened clip readable", failure);
	return ok;
}

MOONLIT_TEST(sqlite_update_keeps_id_and_refreshes_fts)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;
	SqliteClipRepository repository(paths);
	if (!repository.open(&error)) {
		*failure = QStringLiteral("open failed: %1").arg(error);
		return false;
	}

	const QString mediaPath = paths.clipsPath() + QStringLiteral("/session-2026-08-02-0003.mkv");
	if (!createDummyFile(mediaPath, failure)) {
		return false;
	}

	const auto first = repository.upsert(makeClip(mediaPath, QStringLiteral("Alpha Title")), &error);
	if (!first) {
		*failure = QStringLiteral("first upsert failed: %1").arg(error);
		return false;
	}

	const auto updated = repository.upsert(makeClip(mediaPath, QStringLiteral("Beta Title")), &error);
	if (!updated) {
		*failure = QStringLiteral("second upsert failed: %1").arg(error);
		return false;
	}

	bool ok = expect(updated->id == first->id, "update keeps the original id", failure);
	ok &= expect(repository.list().size() == 1, "update does not duplicate the record", failure);
	ok &= expect(repository.search(QStringLiteral("alpha")).isEmpty(), "FTS drops the old title", failure);
	ok &= expect(repository.search(QStringLiteral("beta")).size() == 1, "FTS finds the new title", failure);
	return ok;
}

MOONLIT_TEST(sqlite_reconcile_tracks_missing_files)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;
	SqliteClipRepository repository(paths);
	if (!repository.open(&error)) {
		*failure = QStringLiteral("open failed: %1").arg(error);
		return false;
	}

	const QString mediaPath = paths.clipsPath() + QStringLiteral("/session-2026-08-02-0004.mkv");
	if (!createDummyFile(mediaPath, failure)) {
		return false;
	}

	const auto saved = repository.upsert(makeClip(mediaPath, QStringLiteral("Reconcile Me")), &error);
	if (!saved) {
		*failure = QStringLiteral("upsert failed: %1").arg(error);
		return false;
	}

	QFile::remove(mediaPath);
	ReconcileSummary summary;
	if (!repository.reconcile(&summary, &error)) {
		*failure = QStringLiteral("reconcile failed: %1").arg(error);
		return false;
	}
	bool ok = expect(summary.scanned == 1 && summary.nowMissing == 1 && summary.restored == 0,
			 "reconcile marks the clip missing", failure);
	const auto afterDelete = repository.find(saved->id);
	ok &= expect(afterDelete && afterDelete->missing, "clip is flagged missing", failure);
	ok &= expect(afterDelete && afterDelete->fileSize == -1, "missing clip has no file size", failure);

	if (!createDummyFile(mediaPath, failure)) {
		return false;
	}
	ReconcileSummary restored;
	if (!repository.reconcile(&restored, &error)) {
		*failure = QStringLiteral("second reconcile failed: %1").arg(error);
		return false;
	}
	ok &= expect(restored.scanned == 1 && restored.nowMissing == 0 && restored.restored == 1,
		     "reconcile restores the clip", failure);
	const auto afterRestore = repository.find(saved->id);
	ok &= expect(afterRestore && !afterRestore->missing, "clip is present again", failure);
	return ok;
}
