#include "JsonImport.hpp"

#include "ClipJson.hpp"

extern "C" {
#include <sqlite3.h>
}

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <moonlit/Clip.hpp>

namespace MoonLit {
namespace {

const char *kInsertSql =
	"INSERT INTO clips (id, title, media_path, thumbnail_path, created_at, file_size, file_modified_at, "
	"trim_start_ms, trim_end_ms, gain_db, muted, missing, duration_ms, width, height, frame_rate, bit_rate, "
	"video_stream_count, audio_stream_count, has_audio, container, video_codec, audio_codec) "
	"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23);";

void bindClip(sqlite3_stmt *statement, const Clip &clip)
{
	const QString createdAt = clip.createdAtUtc.toUTC().toString(Qt::ISODateWithMs);
	const QString fileModifiedAt = clip.fileModifiedAtUtc.toUTC().toString(Qt::ISODateWithMs);

	sqlite3_bind_text(statement, 1, clip.id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, 2, clip.title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, 3, clip.mediaPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, 4, clip.thumbnailPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, 5, createdAt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(statement, 6, clip.fileSize);
	sqlite3_bind_text(statement, 7, fileModifiedAt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(statement, 8, clip.trimStartMs);
	sqlite3_bind_int64(statement, 9, clip.trimEndMs);
	sqlite3_bind_double(statement, 10, clip.gainDb);
	sqlite3_bind_int(statement, 11, clip.muted ? 1 : 0);
	sqlite3_bind_int(statement, 12, clip.missing ? 1 : 0);
	sqlite3_bind_int64(statement, 13, clip.metadata.durationMs);
	sqlite3_bind_int(statement, 14, clip.metadata.width);
	sqlite3_bind_int(statement, 15, clip.metadata.height);
	sqlite3_bind_double(statement, 16, clip.metadata.frameRate);
	sqlite3_bind_int64(statement, 17, clip.metadata.bitRate);
	sqlite3_bind_int(statement, 18, clip.metadata.videoStreamCount);
	sqlite3_bind_int(statement, 19, clip.metadata.audioStreamCount);
	sqlite3_bind_int(statement, 20, clip.metadata.hasAudio ? 1 : 0);
	sqlite3_bind_text(statement, 21, clip.metadata.container.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, 22, clip.metadata.videoCodec.toUtf8().constData(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, 23, clip.metadata.audioCodec.toUtf8().constData(), -1, SQLITE_TRANSIENT);
}

} // namespace

bool JsonImport::importClips(sqlite3 *db, const QString &indexPath, QString *error)
{
	QFile file(indexPath);
	if (!file.exists()) {
		return true;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		detail::setError(error, QStringLiteral("Unable to read clip index: %1").arg(file.errorString()));
		return false;
	}

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		detail::setError(error, QStringLiteral("Invalid clip index: %1").arg(parseError.errorString()));
		return false;
	}

	const QJsonArray records = document.object().value(QStringLiteral("clips")).toArray();
	if (records.isEmpty()) {
		return true;
	}

	char *message = nullptr;
	if (sqlite3_exec(db, "BEGIN;", nullptr, nullptr, &message) != SQLITE_OK) {
		detail::setError(error, QStringLiteral("SQLite: %1").arg(message ? QString::fromUtf8(message) : QString()));
		sqlite3_free(message);
		return false;
	}

	bool failed = false;
	QString failure;
	sqlite3_stmt *statement = nullptr;
	if (sqlite3_prepare_v2(db, kInsertSql, -1, &statement, nullptr) != SQLITE_OK) {
		failure = QStringLiteral("SQLite: %1").arg(sqlite3_errmsg(db));
		failed = true;
	}

	for (const QJsonValue &record : records) {
		if (failed || !record.isObject()) {
			continue;
		}

		Clip clip;
		if (!detail::fromJson(record.toObject(), clip)) {
			continue;
		}

		sqlite3_reset(statement);
		sqlite3_clear_bindings(statement);
		bindClip(statement, clip);
		if (sqlite3_step(statement) != SQLITE_DONE) {
			failure = QStringLiteral("SQLite: %1").arg(sqlite3_errmsg(db));
			failed = true;
		}
	}

	if (statement) {
		sqlite3_finalize(statement);
	}

	const char *endSql = failed ? "ROLLBACK;" : "COMMIT;";
	if (sqlite3_exec(db, endSql, nullptr, nullptr, &message) != SQLITE_OK) {
		detail::setError(error, QStringLiteral("SQLite: %1").arg(message ? QString::fromUtf8(message) : QString()));
		sqlite3_free(message);
		return false;
	}
	if (failed) {
		detail::setError(error, failure);
		return false;
	}
	return true;
}

} // namespace MoonLit
