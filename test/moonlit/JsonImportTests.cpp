#include "MoonLitTest.hpp"

#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/persistence/SqliteClipRepository.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

extern "C" {
#include <sqlite3.h>
}

using namespace MoonLit;
using namespace MoonLitTest;

namespace {

QJsonObject jsonRecord(const QString &id, const QString &title, const QString &mediaPath)
{
	QJsonObject metadata;
	metadata.insert(QStringLiteral("durationMs"), 10000);
	metadata.insert(QStringLiteral("width"), 1920);
	metadata.insert(QStringLiteral("height"), 1080);
	metadata.insert(QStringLiteral("frameRate"), 60.0);
	metadata.insert(QStringLiteral("hasAudio"), true);
	metadata.insert(QStringLiteral("container"), QStringLiteral("mkv"));
	metadata.insert(QStringLiteral("videoCodec"), QStringLiteral("h264"));
	metadata.insert(QStringLiteral("audioCodec"), QStringLiteral("aac"));

	QJsonObject object;
	object.insert(QStringLiteral("id"), id);
	object.insert(QStringLiteral("title"), title);
	object.insert(QStringLiteral("mediaPath"), mediaPath);
	object.insert(QStringLiteral("thumbnailPath"), QString());
	object.insert(QStringLiteral("createdAtUtc"), QStringLiteral("2026-08-01T12:00:00.000Z"));
	object.insert(QStringLiteral("fileSize"), 1234);
	object.insert(QStringLiteral("fileModifiedAtUtc"), QStringLiteral("2026-08-01T12:05:00.000Z"));
	object.insert(QStringLiteral("trimStartMs"), 0);
	object.insert(QStringLiteral("trimEndMs"), 10000);
	object.insert(QStringLiteral("gainDb"), 0.0);
	object.insert(QStringLiteral("muted"), false);
	object.insert(QStringLiteral("metadata"), metadata);
	return object;
}

bool writeIndex(const QString &indexPath, const QJsonArray &records, QString *failure)
{
	QJsonObject document;
	document.insert(QStringLiteral("clips"), records);
	if (!QDir().mkpath(QFileInfo(indexPath).absolutePath())) {
		*failure = QStringLiteral("unable to create index directory: %1").arg(QFileInfo(indexPath).absolutePath());
		return false;
	}
	QFile file(indexPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		*failure = QStringLiteral("unable to write index: %1").arg(indexPath);
		return false;
	}
	file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
	return true;
}

bool seedUserVersionOne(const QString &databasePath, QString *failure)
{
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(QFile::encodeName(databasePath).constData(), &db,
			    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
		*failure = QStringLiteral("unable to seed database: %1").arg(db ? sqlite3_errmsg(db) : QString());
		if (db) {
			sqlite3_close(db);
		}
		return false;
	}
	const bool ok = sqlite3_exec(db, "PRAGMA user_version = 1;", nullptr, nullptr, nullptr) == SQLITE_OK;
	sqlite3_close(db);
	if (!ok) {
		*failure = QStringLiteral("unable to set user_version");
	}
	return ok;
}

int userVersion(const QString &databasePath)
{
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(QFile::encodeName(databasePath).constData(), &db,
			    SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
		if (db) {
			sqlite3_close(db);
		}
		return -1;
	}
	int version = -1;
	sqlite3_stmt *statement = nullptr;
	if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &statement, nullptr) == SQLITE_OK &&
	    sqlite3_step(statement) == SQLITE_ROW) {
		version = sqlite3_column_int(statement, 0);
	}
	if (statement) {
		sqlite3_finalize(statement);
	}
	sqlite3_close(db);
	return version;
}

} // namespace

MOONLIT_TEST(json_import_migrates_legacy_index)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;

	const QString mediaA = paths.clipsPath() + QStringLiteral("/clip-a.mkv");
	const QString mediaB = paths.clipsPath() + QStringLiteral("/clip-b.mkv");

	QJsonArray records;
	records.append(jsonRecord(QStringLiteral("legacy-a"), QStringLiteral("First Imported Clip"), mediaA));
	records.append(jsonRecord(QStringLiteral("legacy-b"), QStringLiteral("Second Imported Clip"), mediaB));
	records.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("legacy-invalid")},
				   {QStringLiteral("title"), QStringLiteral("No Media Path")}});

	if (!writeIndex(paths.indexPath(), records, failure)) {
		return false;
	}
	if (!seedUserVersionOne(paths.databasePath(), failure)) {
		return false;
	}

	SqliteClipRepository repository(paths);
	if (!repository.open(&error)) {
		*failure = QStringLiteral("open failed: %1").arg(error);
		return false;
	}

	const QVector<Clip> clips = repository.list();
	bool ok = expect(clips.size() == 2, "imported exactly the two valid records", failure);
	ok &= expect(repository.search(QStringLiteral("first")).size() == 1, "search hits an imported clip", failure);
	ok &= expect(userVersion(paths.databasePath()) == 2, "schema version bumped to 2", failure);
	ok &= expect(!QFile::exists(paths.indexPath()) && QFile::exists(paths.indexPath() + QStringLiteral(".migrated")),
		     "legacy index preserved as .migrated", failure);
	return ok;
}
