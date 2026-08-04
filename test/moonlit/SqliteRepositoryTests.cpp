#include "MoonLitTest.hpp"

#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/persistence/SqliteClipRepository.hpp>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <chrono>
#include <thread>

extern "C" {
#include <sqlite3.h>
}

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

/* The schema as it existed in user_version 2, without the v3 tables. */
const char *kV2Schema = R"(
CREATE TABLE IF NOT EXISTS clips (
	id TEXT PRIMARY KEY,
	title TEXT NOT NULL,
	media_path TEXT NOT NULL UNIQUE,
	thumbnail_path TEXT NOT NULL DEFAULT '',
	created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
	file_size INTEGER NOT NULL DEFAULT -1,
	file_modified_at TEXT NOT NULL DEFAULT '',
	trim_start_ms INTEGER NOT NULL DEFAULT 0,
	trim_end_ms INTEGER NOT NULL DEFAULT -1,
	gain_db REAL NOT NULL DEFAULT 0.0,
	muted INTEGER NOT NULL DEFAULT 0,
	missing INTEGER NOT NULL DEFAULT 0,
	duration_ms INTEGER NOT NULL DEFAULT -1,
	width INTEGER NOT NULL DEFAULT 0,
	height INTEGER NOT NULL DEFAULT 0,
	frame_rate REAL NOT NULL DEFAULT 0.0,
	bit_rate INTEGER NOT NULL DEFAULT 0,
	video_stream_count INTEGER NOT NULL DEFAULT 0,
	audio_stream_count INTEGER NOT NULL DEFAULT 0,
	has_audio INTEGER NOT NULL DEFAULT 0,
	container TEXT NOT NULL DEFAULT '',
	video_codec TEXT NOT NULL DEFAULT '',
	audio_codec TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_clips_created ON clips(created_at);
CREATE VIRTUAL TABLE IF NOT EXISTS clips_fts USING fts5(
	title, media_path, content='clips', content_rowid='rowid', tokenize='unicode61'
);
CREATE TRIGGER IF NOT EXISTS clips_ai AFTER INSERT ON clips BEGIN
	INSERT INTO clips_fts(rowid, title, media_path) VALUES (new.rowid, new.title, new.media_path);
END;
CREATE TRIGGER IF NOT EXISTS clips_ad AFTER DELETE ON clips BEGIN
	INSERT INTO clips_fts(clips_fts, rowid, title, media_path) VALUES ('delete', old.rowid, old.title, old.media_path);
END;
CREATE TRIGGER IF NOT EXISTS clips_au AFTER UPDATE ON clips BEGIN
	INSERT INTO clips_fts(clips_fts, rowid, title, media_path) VALUES ('delete', old.rowid, old.title, old.media_path);
	INSERT INTO clips_fts(rowid, title, media_path) VALUES (new.rowid, new.title, new.media_path);
END;
)";

bool openRaw(sqlite3 **db, const QString &path)
{
	return sqlite3_open(QFile::encodeName(path).constData(), db) == SQLITE_OK;
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

MOONLIT_TEST(sqlite_busy_timeout_waits_for_writer)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;
	SqliteClipRepository repository(paths);
	if (!repository.open(&error)) {
		*failure = QStringLiteral("open failed: %1").arg(error);
		return false;
	}

	/* A second connection (another process) grabs the write lock with no
	 * busy timeout of its own. */
	sqlite3 *blocker = nullptr;
	if (!openRaw(&blocker, paths.databasePath())) {
		*failure = QStringLiteral("unable to open blocking connection");
		return false;
	}
	sqlite3_busy_timeout(blocker, 0);
	if (sqlite3_exec(blocker, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
		sqlite3_close(blocker);
		*failure = QStringLiteral("unable to acquire the write lock");
		return false;
	}

	/* Release the lock shortly after the repository write starts waiting. */
	std::thread releaser([blocker]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		sqlite3_exec(blocker, "ROLLBACK;", nullptr, nullptr, nullptr);
		sqlite3_close(blocker);
	});

	const QString mediaPath = paths.clipsPath() + QStringLiteral("/session-2026-08-03-busy.mkv");
	if (!createDummyFile(mediaPath, failure)) {
		releaser.join();
		return false;
	}

	const auto saved = repository.upsert(makeClip(mediaPath, QStringLiteral("Busy Wait")), &error);
	releaser.join();
	bool ok = expect(saved.has_value(), "upsert waited for the write lock instead of failing", failure);
	if (saved) {
		ok &= expect(repository.find(saved->id).has_value(), "waiting write is durable", failure);
	}
	return ok;
}

MOONLIT_TEST(sqlite_reconcile_discovers_orphans)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;
	SqliteClipRepository repository(paths);
	if (!repository.open(&error)) {
		*failure = QStringLiteral("open failed: %1").arg(error);
		return false;
	}

	const QString knownPath = paths.clipsPath() + QStringLiteral("/session-2026-08-03-known.mkv");
	if (!createDummyFile(knownPath, failure)) {
		return false;
	}
	if (!repository.upsert(makeClip(knownPath, QStringLiteral("Known")), &error)) {
		*failure = QStringLiteral("upsert failed: %1").arg(error);
		return false;
	}

	/* A media file the user dropped straight into the clips directory. */
	const QString orphanPath = paths.clipsPath() + QStringLiteral("/user-dropped-2026-08-03.mkv");
	if (!createDummyFile(orphanPath, failure)) {
		return false;
	}

	ReconcileSummary summary;
	if (!repository.reconcile(&summary, &error)) {
		*failure = QStringLiteral("reconcile failed: %1").arg(error);
		return false;
	}
	bool ok = expect(summary.scanned == 1, "one known record scanned", failure);
	ok &= expect(summary.discovered == 1, "orphan file discovered", failure);
	ok &= expect(repository.list().size() == 2, "orphan indexed into the library", failure);
	const auto byPath = repository.findByMediaPath(orphanPath);
	ok &= expect(byPath.has_value() && !byPath->missing, "orphan record is present and valid", failure);
	return ok;
}

MOONLIT_TEST(sqlite_migrates_v2_to_v3_and_rebuilds_fts)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;

	/* Seed a database exactly as user_version 2 left it. */
	{
		sqlite3 *db = nullptr;
		if (!openRaw(&db, paths.databasePath())) {
			*failure = QStringLiteral("unable to seed v2 database");
			return false;
		}
		if (sqlite3_exec(db, kV2Schema, nullptr, nullptr, nullptr) != SQLITE_OK ||
		    sqlite3_exec(db, "INSERT INTO clips (id, title, media_path) "
				      "VALUES ('v2clip', 'Ancient Clip', 'C:/clips/ancient.mkv');",
				 nullptr, nullptr, nullptr) != SQLITE_OK ||
		    sqlite3_exec(db, "PRAGMA user_version = 2;", nullptr, nullptr, nullptr) != SQLITE_OK) {
			sqlite3_close(db);
			*failure = QStringLiteral("unable to seed v2 records");
			return false;
		}
		sqlite3_close(db);
	}

	{
		SqliteClipRepository repository(paths);
		if (!repository.open(&error)) {
			*failure = QStringLiteral("open after v2 failed: %1").arg(error);
			return false;
		}
		bool ok = expect(repository.list().size() == 1, "v2 clip survives migration", failure);
		ok &= expect(repository.search(QStringLiteral("ancient")).size() == 1,
			     "FTS rebuilt and searchable after migration", failure);
		ok &= expect(repository.find(QStringLiteral("v2clip")).has_value(), "v2 record readable", failure);
		if (!ok) {
			return false;
		}
	}

	/* The repository is closed: verify schema version and v3 tables. */
	sqlite3 *db = nullptr;
	if (!openRaw(&db, paths.databasePath())) {
		*failure = QStringLiteral("unable to verify migrated database");
		return false;
	}
	bool ok = expect(sqlite3_exec(db, "SELECT 1 FROM timelines LIMIT 1;", nullptr, nullptr, nullptr) == SQLITE_OK,
			 "timelines table exists", failure);
	ok &= expect(sqlite3_exec(db, "SELECT 1 FROM export_jobs LIMIT 1;", nullptr, nullptr, nullptr) == SQLITE_OK,
		     "export_jobs table exists", failure);
	sqlite3_stmt *version = nullptr;
	if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &version, nullptr) == SQLITE_OK &&
	    sqlite3_step(version) == SQLITE_ROW) {
		ok &= expect(sqlite3_column_int(version, 0) == 3, "user_version is 3", failure);
	}
	sqlite3_finalize(version);
	sqlite3_close(db);
	return ok;
}

MOONLIT_TEST(sqlite_export_jobs_round_trip)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;
	SqliteClipRepository repository(paths);
	if (!repository.open(&error)) {
		*failure = QStringLiteral("open failed: %1").arg(error);
		return false;
	}

	const auto first = repository.enqueueExportJob(QStringLiteral("trim"), QStringLiteral("{\"clip\":\"a\"}"), &error);
	const auto second =
		repository.enqueueExportJob(QStringLiteral("timeline"), QStringLiteral("{\"segments\":2}"), &error);
	bool ok = expect(first.has_value() && second.has_value() && *second == *first + 1,
			 "export jobs get sequential ids", failure);
	ok &= expect(repository.updateExportJob(*first, QStringLiteral("running"), 0.25, QString(), &error),
		     "job can move to running", failure);
	ok &= expect(repository.updateExportJob(*first, QStringLiteral("done"), 1.0, QString(), &error),
		     "job can finish", failure);
	ok &= expect(repository.updateExportJob(*second, QStringLiteral("running"), 0.5, QString(), &error),
		     "second job runs", failure);

	QVector<ExportJobRecord> records = repository.listExportJobs(&error);
	ok &= expect(records.size() == 2, "both jobs listed", failure);
	ok &= expect(records.size() == 2 && records[0].state == QStringLiteral("done") &&
			     records[1].state == QStringLiteral("running"),
		     "job states persisted", failure);

	/* Simulated crash: the process died with a job in "running". */
	ok &= expect(repository.failInterruptedExportJobs(&error), "interrupted jobs can be failed", failure);
	records = repository.listExportJobs(&error);
	ok &= expect(records.size() == 2 && records[1].state == QStringLiteral("failed") &&
			     records[1].error == QStringLiteral("interrupted by restart"),
		     "running job failed on restart", failure);
	ok &= expect(records.size() == 2 && records[0].state == QStringLiteral("done"),
		     "finished job untouched", failure);
	ok &= expect(records[1].finishedAtUtc.isValid(), "failed job gets a finished stamp", failure);
	return ok;
}
