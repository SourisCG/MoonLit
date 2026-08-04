#pragma once

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

namespace MoonLit {

class SqliteClipRepository;

/* Serial export worker for trim and timeline jobs. Runs on its own thread at
 * below-normal priority; jobs are persisted in the export_jobs table so the
 * queue survives restarts ("running" jobs are failed at startup). */
class ExportQueue final : public QObject {
	Q_OBJECT

public:
	explicit ExportQueue(SqliteClipRepository *repository, QObject *parent = nullptr);
	~ExportQueue() override;

	ExportQueue(const ExportQueue &) = delete;
	ExportQueue &operator=(const ExportQueue &) = delete;

	/* Moves the queue onto its worker thread and drains persisted jobs. */
	void start();
	/* Cancels the active export and stops the worker. */
	void shutdown();

	/* Thread-safe entry points (called from the UI thread). */
	void enqueueTrim(const QString &clipId, qint64 startMs, qint64 endMs);
	void enqueueTimeline(const QString &timelineId);
	void cancelCurrent() { cancelCurrent_.store(true); }

signals:
	void exportProgress(double fraction);
	void exportFinished(bool succeeded, bool cancelled, const QString &outputPath, const QString &error);

private slots:
	void onStart();
	void onEnqueueTrim(const QString &clipId, qint64 startMs, qint64 endMs);
	void onEnqueueTimeline(const QString &timelineId);
	void processNext();

private:
	bool runExportJob(qint64 jobId, const QString &kind, const QString &paramsJson, QString *error);

	SqliteClipRepository *repository_ = nullptr;
	QThread workerThread_;
	std::atomic_bool cancelCurrent_{false};
	bool processing_ = false;
	bool started_ = false;
};

} // namespace MoonLit
