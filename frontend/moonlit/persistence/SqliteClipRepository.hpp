#pragma once

#include "ClipRepository.hpp"

#include <moonlit/MoonLitPaths.hpp>

#include <optional>

struct sqlite3;

namespace MoonLit {

/* SQLite-backed repository (WAL, user_version 2). The previous JSON index
 * (index.json) is imported once on open and preserved as index.json.migrated.
 * Search uses the FTS5 virtual table kept in sync by triggers. */
class SqliteClipRepository final : public ClipRepository {
public:
	explicit SqliteClipRepository(MoonLitPaths paths);
	~SqliteClipRepository() override;

	SqliteClipRepository(const SqliteClipRepository &) = delete;
	SqliteClipRepository &operator=(const SqliteClipRepository &) = delete;
	SqliteClipRepository(SqliteClipRepository &&) = delete;
	SqliteClipRepository &operator=(SqliteClipRepository &&) = delete;

	bool open(QString *error = nullptr) override;
	bool reload(QString *error = nullptr) override;
	QVector<Clip> list(bool includeMissing = true) const override;
	QVector<Clip> search(const QString &query, bool includeMissing = true) const override;
	std::optional<Clip> find(const QString &id) const override;
	std::optional<Clip> findByMediaPath(const QString &mediaPath) const override;
	std::optional<Clip> upsert(Clip clip, QString *error = nullptr) override;
	bool remove(const QString &id, QString *error = nullptr) override;
	bool reconcile(ReconcileSummary *summary = nullptr, QString *error = nullptr) override;

private:
	bool close();
	bool migrate(QString *error);

	MoonLitPaths paths_;
	sqlite3 *db_ = nullptr;
};

} // namespace MoonLit
