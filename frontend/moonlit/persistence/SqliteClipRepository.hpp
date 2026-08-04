#pragma once

#include "ClipRepository.hpp"

#include <moonlit/editor/Timeline.hpp>
#include <moonlit/MoonLitPaths.hpp>

#include <optional>

struct sqlite3;

namespace MoonLit {

/* SQLite-backed repository (WAL, user_version 3). The previous JSON index
 * (index.json) is imported once on open and preserved as index.json.migrated.
 * Search uses the FTS5 virtual table kept in sync by triggers. One logical
 * writer owns this connection; every mutating call runs on that writer.
 *
 * Reconcile runs in a single IMMEDIATE transaction: file-state refresh plus
 * orphan discovery (media files inside the clips directory that have no
 * record yet). Export jobs are persisted here so the export queue survives
 * restarts. */
struct ExportJobRecord {
	qint64 id{0};
	QString kind; /* "trim" | "timeline" */
	QString params; /* JSON payload */
	QString state; /* queued | running | done | failed | cancelled */
	double progress{0.0};
	QString error;
	QDateTime createdAtUtc;
	QDateTime finishedAtUtc;
};

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

	/* Persistent export queue. All writes stay on the repository connection. */
	std::optional<qint64> enqueueExportJob(const QString &kind, const QString &paramsJson, QString *error = nullptr);
	bool updateExportJob(qint64 jobId, const QString &state, double progress = -1.0,
			     const QString &jobError = QString(), QString *error = nullptr);
	QVector<ExportJobRecord> listExportJobs(QString *error = nullptr) const;
	/* Marks jobs left in "running" by a previous process as "failed". */
	bool failInterruptedExportJobs(QString *error = nullptr);

	/* Timelines: segments are stored as JSON inside the timelines table. */
	bool saveTimeline(const TimelineProject &project, QString *error = nullptr);
	std::optional<TimelineProject> loadTimeline(const QString &id, QString *error = nullptr) const;
	QVector<TimelineProject> listTimelines(QString *error = nullptr) const;
	bool deleteTimeline(const QString &id, QString *error = nullptr);

private:
	bool close();
	bool migrate(QString *error);
	bool reconcileDiscover(ReconcileSummary *summary, QString *error);

	MoonLitPaths paths_;
	sqlite3 *db_ = nullptr;
};

} // namespace MoonLit
