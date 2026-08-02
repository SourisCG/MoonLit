#include "SqliteClipRepository.hpp"

#include "ClipJson.hpp"
#include "JsonImport.hpp"

extern "C" {
#include <sqlite3.h>
}

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QUuid>

#include <utility>

namespace MoonLit {

namespace {

constexpr int kSchemaVersion = 2;

const char *kSchema = R"(
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

const char *kSelectColumns =
	"id, title, media_path, thumbnail_path, created_at, file_size, file_modified_at, "
	"trim_start_ms, trim_end_ms, gain_db, muted, missing, "
	"duration_ms, width, height, frame_rate, bit_rate, "
	"video_stream_count, audio_stream_count, has_audio, container, video_codec, audio_codec";

QString textAt(sqlite3_stmt *statement, int column)
{
	const unsigned char *text = sqlite3_column_text(statement, column);
	return text ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
}

QString isoString(const QDateTime &dateTime)
{
	return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime isoToUtc(const QString &value)
{
	QDateTime dateTime = QDateTime::fromString(value, Qt::ISODateWithMs);
	if (!dateTime.isValid()) {
		dateTime = QDateTime::fromString(value, Qt::ISODate);
	}
	return dateTime.isValid() ? dateTime.toUTC() : QDateTime();
}

Clip clipFromRow(sqlite3_stmt *statement)
{
	Clip clip;
	int column = 0;
	clip.id = textAt(statement, column++);
	clip.title = textAt(statement, column++);
	clip.mediaPath = textAt(statement, column++);
	clip.thumbnailPath = textAt(statement, column++);
	clip.createdAtUtc = isoToUtc(textAt(statement, column++));
	clip.fileSize = sqlite3_column_int64(statement, column++);
	clip.fileModifiedAtUtc = isoToUtc(textAt(statement, column++));
	clip.trimStartMs = sqlite3_column_int64(statement, column++);
	clip.trimEndMs = sqlite3_column_int64(statement, column++);
	clip.gainDb = sqlite3_column_double(statement, column++);
	clip.muted = sqlite3_column_int(statement, column++) != 0;
	clip.missing = sqlite3_column_int(statement, column++) != 0;
	clip.metadata.durationMs = sqlite3_column_int64(statement, column++);
	clip.metadata.width = sqlite3_column_int(statement, column++);
	clip.metadata.height = sqlite3_column_int(statement, column++);
	clip.metadata.frameRate = sqlite3_column_double(statement, column++);
	clip.metadata.bitRate = sqlite3_column_int64(statement, column++);
	clip.metadata.videoStreamCount = sqlite3_column_int(statement, column++);
	clip.metadata.audioStreamCount = sqlite3_column_int(statement, column++);
	clip.metadata.hasAudio = sqlite3_column_int(statement, column++) != 0;
	clip.metadata.container = textAt(statement, column++);
	clip.metadata.videoCodec = textAt(statement, column++);
	clip.metadata.audioCodec = textAt(statement, column++);
	return clip;
}

void bindClip(sqlite3_stmt *statement, const Clip &clip, int startColumn = 1)
{
	int column = startColumn;
	sqlite3_bind_text(statement, column++, clip.id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, column++, clip.title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, column++, clip.mediaPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, column++, clip.thumbnailPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, column++, isoString(clip.createdAtUtc).toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(statement, column++, clip.fileSize);
	sqlite3_bind_text(statement, column++, isoString(clip.fileModifiedAtUtc).toUtf8().constData(), -1,
			  SQLITE_TRANSIENT);
	sqlite3_bind_int64(statement, column++, clip.trimStartMs);
	sqlite3_bind_int64(statement, column++, clip.trimEndMs);
	sqlite3_bind_double(statement, column++, clip.gainDb);
	sqlite3_bind_int(statement, column++, clip.muted ? 1 : 0);
	sqlite3_bind_int(statement, column++, clip.missing ? 1 : 0);
	sqlite3_bind_int64(statement, column++, clip.metadata.durationMs);
	sqlite3_bind_int(statement, column++, clip.metadata.width);
	sqlite3_bind_int(statement, column++, clip.metadata.height);
	sqlite3_bind_double(statement, column++, clip.metadata.frameRate);
	sqlite3_bind_int64(statement, column++, clip.metadata.bitRate);
	sqlite3_bind_int(statement, column++, clip.metadata.videoStreamCount);
	sqlite3_bind_int(statement, column++, clip.metadata.audioStreamCount);
	sqlite3_bind_int(statement, column++, clip.metadata.hasAudio ? 1 : 0);
	sqlite3_bind_text(statement, column++, clip.metadata.container.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, column++, clip.metadata.videoCodec.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, column++, clip.metadata.audioCodec.toUtf8().constData(), -1, SQLITE_TRANSIENT);
}

struct Statement {
	sqlite3_stmt *stmt = nullptr;

	Statement() = default;
	Statement(const Statement &) = delete;
	Statement &operator=(const Statement &) = delete;

	~Statement()
	{
		if (stmt) {
			sqlite3_finalize(stmt);
		}
	}
};

bool step(sqlite3 *db, sqlite3_stmt *statement, QString *error)
{
	const int rc = sqlite3_step(statement);
	if (rc == SQLITE_DONE || rc == SQLITE_ROW) {
		return true;
	}
	detail::setError(error, QStringLiteral("SQLite: %1").arg(sqlite3_errmsg(db)));
	return false;
}

bool execute(sqlite3 *db, const char *sql, QString *error)
{
	char *message = nullptr;
	const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
	if (rc != SQLITE_OK) {
		detail::setError(error, QStringLiteral("SQLite: %1")
					     .arg(message ? QString::fromUtf8(message) : QString::fromUtf8(sqlite3_errmsg(db))));
		sqlite3_free(message);
		return false;
	}
	return true;
}

bool prepare(sqlite3 *db, sqlite3_stmt **statement, const char *sql, QString *error)
{
	const int rc = sqlite3_prepare_v2(db, sql, -1, statement, nullptr);
	if (rc != SQLITE_OK) {
		detail::setError(error, QStringLiteral("SQLite: %1").arg(sqlite3_errmsg(db)));
		return false;
	}
	return true;
}

} // namespace

SqliteClipRepository::SqliteClipRepository(MoonLitPaths paths) : paths_(std::move(paths)) {}

SqliteClipRepository::~SqliteClipRepository()
{
	close();
}

bool SqliteClipRepository::close()
{
	if (db_) {
		sqlite3_close(db_);
		db_ = nullptr;
	}
	return true;
}

bool SqliteClipRepository::open(QString *error)
{
	if (db_) {
		return true;
	}
	if (!paths_.ensureDirectories(error)) {
		return false;
	}

	const int openResult = sqlite3_open_v2(QFile::encodeName(paths_.databasePath()).constData(), &db_,
					       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
					       nullptr);
	if (openResult != SQLITE_OK) {
		detail::setError(error, QStringLiteral("SQLite: %1").arg(db_ ? sqlite3_errmsg(db_) : QStringLiteral("open failed")));
		close();
		return false;
	}

	if (!execute(db_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", error)) {
		close();
		return false;
	}
	if (!execute(db_, kSchema, error)) {
		close();
		return false;
	}
	if (!migrate(error)) {
		close();
		return false;
	}
	return true;
}

bool SqliteClipRepository::migrate(QString *error)
{
	int version = 0;
	{
		Statement query;
		if (!prepare(db_, &query.stmt, "PRAGMA user_version;", error)) {
			return false;
		}
		if (sqlite3_step(query.stmt) == SQLITE_ROW) {
			version = sqlite3_column_int(query.stmt, 0);
		}
	}

	if (version >= kSchemaVersion) {
		return true;
	}

	if (version < 1) {
		// Version 0: a freshly created database. Nothing to import.
		return execute(db_, "PRAGMA user_version = 2;", error);
	}

	// Version 1 was the JSON index. Import it once, then preserve the file.
	if (version < 2) {
		const QString indexPath = paths_.indexPath();
		if (QFile::exists(indexPath)) {
			if (!JsonImport::importClips(db_, indexPath, error)) {
				return false;
			}
			QFile::rename(indexPath, indexPath + QStringLiteral(".migrated"));
		}
		if (!execute(db_, "PRAGMA user_version = 2;", error)) {
			return false;
		}
	}
	return true;
}

bool SqliteClipRepository::reload(QString *error)
{
	if (!close()) {
		return false;
	}
	return open(error);
}

QVector<Clip> SqliteClipRepository::list(bool includeMissing) const
{
	QVector<Clip> result;
	if (!db_) {
		return result;
	}

	Statement query;
	const QString sql = QStringLiteral(
				     "SELECT %1 FROM clips"
				     "%2"
				     " ORDER BY created_at DESC, id ASC;")
				     .arg(QString::fromLatin1(kSelectColumns),
					  includeMissing ? QString() : QStringLiteral(" WHERE missing = 0"));
	if (sqlite3_prepare_v2(db_, sql.toUtf8().constData(), -1, &query.stmt, nullptr) != SQLITE_OK) {
		return result;
	}

	while (sqlite3_step(query.stmt) == SQLITE_ROW) {
		result.append(clipFromRow(query.stmt));
	}
	return result;
}

QVector<Clip> SqliteClipRepository::search(const QString &query, bool includeMissing) const
{
	const QString trimmed = query.trimmed();
	if (trimmed.isEmpty() || !db_) {
		return list(includeMissing);
	}

	QString match = trimmed;
	match.remove(QLatin1Char('"'));
	const QString expression = QStringLiteral("\"%1\"*").arg(match);

	QVector<Clip> result;
	Statement ftsQuery;
	QStringList columns = QString::fromLatin1(kSelectColumns).split(QLatin1String(", "), Qt::SkipEmptyParts);
	for (QString &column : columns) {
		column.prepend(QStringLiteral("c."));
	}
	const QString sql = QStringLiteral(
				     "SELECT %1 FROM clips_fts f JOIN clips c ON c.rowid = f.rowid "
				     "WHERE clips_fts MATCH ?%2 ORDER BY rank, c.created_at DESC, c.id ASC;")
				     .arg(columns.join(QStringLiteral(", ")),
					  includeMissing ? QString() : QStringLiteral(" AND c.missing = 0"));
	if (sqlite3_prepare_v2(db_, sql.toUtf8().constData(), -1, &ftsQuery.stmt, nullptr) != SQLITE_OK) {
		return list(includeMissing);
	}
	sqlite3_bind_text(ftsQuery.stmt, 1, expression.toUtf8().constData(), -1, SQLITE_TRANSIENT);

	int rc;
	while ((rc = sqlite3_step(ftsQuery.stmt)) == SQLITE_ROW) {
		result.append(clipFromRow(ftsQuery.stmt));
	}
	if (rc == SQLITE_ERROR) {
		return list(includeMissing);
	}
	return result;
}

std::optional<Clip> SqliteClipRepository::find(const QString &id) const
{
	if (!db_ || id.isEmpty()) {
		return std::nullopt;
	}

	Statement query;
	const QString sql = QStringLiteral("SELECT %1 FROM clips WHERE id = ?1;").arg(QString::fromLatin1(kSelectColumns));
	if (sqlite3_prepare_v2(db_, sql.toUtf8().constData(), -1, &query.stmt, nullptr) != SQLITE_OK) {
		return std::nullopt;
	}
	sqlite3_bind_text(query.stmt, 1, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	if (sqlite3_step(query.stmt) != SQLITE_ROW) {
		return std::nullopt;
	}
	return clipFromRow(query.stmt);
}

std::optional<Clip> SqliteClipRepository::findByMediaPath(const QString &mediaPath) const
{
	if (!db_ || mediaPath.isEmpty()) {
		return std::nullopt;
	}

	Statement query;
	const QString sql = QStringLiteral("SELECT %1 FROM clips WHERE media_path = ?1 COLLATE NOCASE;")
				    .arg(QString::fromLatin1(kSelectColumns));
	if (sqlite3_prepare_v2(db_, sql.toUtf8().constData(), -1, &query.stmt, nullptr) != SQLITE_OK) {
		return std::nullopt;
	}
	sqlite3_bind_text(query.stmt, 1, detail::normalizedPath(mediaPath).toUtf8().constData(), -1, SQLITE_TRANSIENT);
	if (sqlite3_step(query.stmt) != SQLITE_ROW) {
		return std::nullopt;
	}
	return clipFromRow(query.stmt);
}

std::optional<Clip> SqliteClipRepository::upsert(Clip clip, QString *error)
{
	if (!db_) {
		detail::setError(error, QStringLiteral("Clip repository is not open"));
		return std::nullopt;
	}
	if (clip.mediaPath.isEmpty()) {
		detail::setError(error, QStringLiteral("A clip must have a media path"));
		return std::nullopt;
	}

	clip.mediaPath = detail::normalizedPath(clip.mediaPath);
	if (clip.id.isEmpty()) {
		clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	}
	if (!clip.createdAtUtc.isValid()) {
		clip.createdAtUtc = QDateTime::currentDateTimeUtc();
	}
	if (clip.title.isEmpty()) {
		clip.title = QFileInfo(clip.mediaPath).completeBaseName();
	}
	if (clip.thumbnailPath.isEmpty()) {
		clip.thumbnailPath = paths_.thumbnailPath(clip.id);
	}
	detail::refreshFileState(clip);

	QString existingId;
	{
		Statement lookup;
		if (!prepare(db_, &lookup.stmt, "SELECT id, media_path FROM clips WHERE id = ?1 OR media_path = ?2 COLLATE NOCASE;",
			      error)) {
			return std::nullopt;
		}
		sqlite3_bind_text(lookup.stmt, 1, clip.id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(lookup.stmt, 2, clip.mediaPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
		while (sqlite3_step(lookup.stmt) == SQLITE_ROW) {
			const QString rowId = textAt(lookup.stmt, 0);
			if (existingId.isEmpty()) {
				existingId = rowId;
			} else if (rowId != existingId) {
				detail::setError(error, QStringLiteral("Clip id and media path refer to different records"));
				return std::nullopt;
			}
		}
	}

	Statement write;
	const bool isUpdate = !existingId.isEmpty();
	const QString sql = isUpdate
			    ? QStringLiteral("UPDATE clips SET title = ?2, media_path = ?3, thumbnail_path = ?4, "
					     "created_at = ?5, file_size = ?6, file_modified_at = ?7, trim_start_ms = ?8, "
					     "trim_end_ms = ?9, gain_db = ?10, muted = ?11, missing = ?12, duration_ms = ?13, "
					     "width = ?14, height = ?15, frame_rate = ?16, bit_rate = ?17, "
					     "video_stream_count = ?18, audio_stream_count = ?19, has_audio = ?20, "
					     "container = ?21, video_codec = ?22, audio_codec = ?23 WHERE id = ?1;")
			    : QStringLiteral("INSERT INTO clips (%1) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, "
					     "?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23);")
				      .arg(QString::fromLatin1(kSelectColumns));
	if (!prepare(db_, &write.stmt, sql.toUtf8().constData(), error)) {
		return std::nullopt;
	}

	clip.id = existingId.isEmpty() ? clip.id : existingId;
	bindClip(write.stmt, clip);
	if (!step(db_, write.stmt, error)) {
		return std::nullopt;
	}
	return clip;
}

bool SqliteClipRepository::remove(const QString &id, QString *error)
{
	if (!db_) {
		detail::setError(error, QStringLiteral("Clip repository is not open"));
		return false;
	}

	Statement write;
	if (!prepare(db_, &write.stmt, "DELETE FROM clips WHERE id = ?1;", error)) {
		return false;
	}
	sqlite3_bind_text(write.stmt, 1, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	if (sqlite3_step(write.stmt) != SQLITE_DONE) {
		detail::setError(error, QStringLiteral("SQLite: %1").arg(sqlite3_errmsg(db_)));
		return false;
	}
	if (sqlite3_changes(db_) == 0) {
		detail::setError(error, QStringLiteral("Clip not found: %1").arg(id));
		return false;
	}
	return true;
}

bool SqliteClipRepository::reconcile(ReconcileSummary *summary, QString *error)
{
	if (!db_) {
		detail::setError(error, QStringLiteral("Clip repository is not open"));
		return false;
	}

	ReconcileSummary result;
	Statement query;
	const QString sql =
		QStringLiteral("SELECT id, media_path, file_size, file_modified_at, missing FROM clips;");
	if (!prepare(db_, &query.stmt, sql.toUtf8().constData(), error)) {
		return false;
	}

	Statement update;
	const QString updateSql = QStringLiteral("UPDATE clips SET file_size = ?2, file_modified_at = ?3, missing = ?4 WHERE id = ?1;");
	if (!prepare(db_, &update.stmt, updateSql.toUtf8().constData(), error)) {
		return false;
	}

	while (sqlite3_step(query.stmt) == SQLITE_ROW) {
		Clip clip;
		clip.id = textAt(query.stmt, 0);
		clip.mediaPath = textAt(query.stmt, 1);
		clip.fileSize = sqlite3_column_int64(query.stmt, 2);
		clip.fileModifiedAtUtc = isoToUtc(textAt(query.stmt, 3));
		clip.missing = sqlite3_column_int(query.stmt, 4) != 0;

		const bool wasMissing = clip.missing;
		const qint64 previousFileSize = clip.fileSize;
		const QDateTime previousFileModifiedAtUtc = clip.fileModifiedAtUtc;
		detail::refreshFileState(clip);
		++result.scanned;
		if (!wasMissing && clip.missing) {
			++result.nowMissing;
		} else if (wasMissing && !clip.missing) {
			++result.restored;
		}

		if (wasMissing != clip.missing || previousFileSize != clip.fileSize ||
		    previousFileModifiedAtUtc != clip.fileModifiedAtUtc) {
			sqlite3_reset(update.stmt);
			sqlite3_clear_bindings(update.stmt);
			sqlite3_bind_text(update.stmt, 1, clip.id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(update.stmt, 2, clip.fileSize);
			sqlite3_bind_text(update.stmt, 3, isoString(clip.fileModifiedAtUtc).toUtf8().constData(), -1,
					  SQLITE_TRANSIENT);
			sqlite3_bind_int(update.stmt, 4, clip.missing ? 1 : 0);
			if (sqlite3_step(update.stmt) != SQLITE_DONE) {
				detail::setError(error, QStringLiteral("SQLite: %1").arg(sqlite3_errmsg(db_)));
				return false;
			}
		}
	}

	if (summary) {
		*summary = result;
	}
	return true;
}

} // namespace MoonLit
