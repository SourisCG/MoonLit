#include "ExportQueue.hpp"

#include <moonlit/editor/ClipExportService.hpp>
#include <moonlit/editor/FfmpegTimelineExporter.hpp>
#include <moonlit/editor/Timeline.hpp>
#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/persistence/SqliteClipRepository.hpp>
#include <moonlit/platform/IPlatformServices.hpp>

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>

namespace MoonLit {

namespace {

struct TrimParams {
	QString clipId;
	qint64 startMs = 0;
	qint64 endMs = -1;
};

bool trimParamsFromJson(const QString &json, TrimParams &params)
{
	const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
	if (!document.isObject()) {
		return false;
	}
	const QJsonObject object = document.object();
	params.clipId = object.value(QStringLiteral("clipId")).toString();
	params.startMs = object.value(QStringLiteral("startMs")).toInteger();
	params.endMs = object.value(QStringLiteral("endMs")).toInteger(-1);
	return !params.clipId.isEmpty();
}

} // namespace

ExportQueue::ExportQueue(SqliteClipRepository *repository, QObject *parent)
	: QObject(parent), repository_(repository)
{
}

ExportQueue::~ExportQueue()
{
	qDebug("MoonLit-DBG ~ExportQueue on thread %p", (void*)QThread::currentThread());
	shutdown();
}

void ExportQueue::start()
{
	if (started_) {
		return;
	}
	started_ = true;
	moveToThread(&workerThread_);
	workerThread_.start();
	QMetaObject::invokeMethod(this, &ExportQueue::onStart, Qt::QueuedConnection);
}

void ExportQueue::shutdown()
{
	if (!started_) {
		return;
	}
	cancelCurrent_.store(true);

	/* The destructor can run on the worker thread itself (deferred delete
	 * processed when the thread finishes): never wait on our own thread. */
	if (QThread::currentThread() == &workerThread_) {
		started_ = false;
		return;
	}

	/* Drain only when the worker loop is reachable from another thread. */
	if (workerThread_.isRunning()) {
		QMetaObject::invokeMethod(this, &ExportQueue::processNext, Qt::BlockingQueuedConnection);
	}
	workerThread_.quit();
	workerThread_.wait();
	started_ = false;
}

void ExportQueue::enqueueTrim(const QString &clipId, qint64 startMs, qint64 endMs)
{
	QMetaObject::invokeMethod(this, [this, clipId, startMs, endMs]() {
		onEnqueueTrim(clipId, startMs, endMs);
	}, Qt::QueuedConnection);
}

void ExportQueue::enqueueTimeline(const QString &timelineId)
{
	QMetaObject::invokeMethod(this, [this, timelineId]() { onEnqueueTimeline(timelineId); },
				  Qt::QueuedConnection);
}

void ExportQueue::onStart()
{
	if (!repository_) {
		return;
	}
	QString error;
	repository_->failInterruptedExportJobs(&error);
	processNext();
}

void ExportQueue::onEnqueueTrim(const QString &clipId, qint64 startMs, qint64 endMs)
{
	if (!repository_) {
		return;
	}
	QJsonObject object;
	object.insert(QStringLiteral("clipId"), clipId);
	object.insert(QStringLiteral("startMs"), startMs);
	object.insert(QStringLiteral("endMs"), endMs);
	QString error;
	repository_->enqueueExportJob(QStringLiteral("trim"),
				      QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)), &error);
	processNext();
}

void ExportQueue::onEnqueueTimeline(const QString &timelineId)
{
	if (!repository_) {
		return;
	}
	QJsonObject object;
	object.insert(QStringLiteral("timelineId"), timelineId);
	QString error;
	repository_->enqueueExportJob(QStringLiteral("timeline"),
				      QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)), &error);
	processNext();
}

void ExportQueue::processNext()
{
	if (!repository_ || processing_) {
		return;
	}

	QString error;
	const QVector<ExportJobRecord> records = repository_->listExportJobs(&error);
	qint64 jobId = -1;
	QString kind;
	QString params;
	for (const ExportJobRecord &record : records) {
		if (record.state == QStringLiteral("queued")) {
			jobId = record.id;
			kind = record.kind;
			params = record.params;
			break;
		}
	}
	if (jobId < 0) {
		return;
	}

	processing_ = true;
	repository_->updateExportJob(jobId, QStringLiteral("running"), 0.0, QString(), &error);

	QString jobError;
	const bool succeeded = runExportJob(jobId, kind, params, &jobError);
	repository_->updateExportJob(jobId, succeeded ? QStringLiteral("done") : QStringLiteral("failed"),
				     succeeded ? 1.0 : 0.0, jobError, &error);
	processing_ = false;

	QMetaObject::invokeMethod(this, &ExportQueue::processNext, Qt::QueuedConnection);
}

bool ExportQueue::runExportJob(qint64 jobId, const QString &kind, const QString &paramsJson, QString *error)
{
	Q_UNUSED(jobId)

	if (kind == QStringLiteral("trim")) {
		TrimParams params;
		if (!trimParamsFromJson(paramsJson, params)) {
			*error = QStringLiteral("Invalid trim job parameters");
			return false;
		}
		const std::optional<Clip> clip = repository_->find(params.clipId);
		if (!clip || clip->missing) {
			*error = QStringLiteral("Clip not available");
			return false;
		}

		const MoonLitPaths paths = MoonLitPaths::defaultPaths();
		ClipExportRequest request;
		request.sourcePath = clip->mediaPath;
		request.destinationPath = paths.exportPath(clip->id, QStringLiteral("mp4"));
		request.startMs = params.startMs;
		request.endMs = params.endMs;
		request.muted = clip->muted;
		request.gainDb = clip->gainDb;
		request.progress = [this](double fraction) { emit exportProgress(fraction); };

		CancelCallback shouldCancel = [this]() { return cancelCurrent_.load(); };
		const ClipExportResult result = FfmpegClipExportService().exportClip(request, shouldCancel);
		emit exportFinished(result.succeeded, result.cancelled, result.outputPath, result.error);
		if (!result.succeeded) {
			*error = result.error;
		}
		return result.succeeded;
	}

	if (kind == QStringLiteral("timeline")) {
		const QJsonDocument document = QJsonDocument::fromJson(paramsJson.toUtf8());
		if (!document.isObject()) {
			*error = QStringLiteral("Invalid timeline job parameters");
			return false;
		}
		const QString timelineId = document.object().value(QStringLiteral("timelineId")).toString();
		const std::optional<TimelineProject> project = repository_->loadTimeline(timelineId, error);
		if (!project) {
			*error = *error + QStringLiteral(" (timeline not found)");
			return false;
		}

		TimelineExportRequest request;
		request.destinationPath = MoonLitPaths::defaultPaths().exportPath(project->id, QStringLiteral("mp4"));
		for (const TimelineSegment &segment : project->segments) {
			const std::optional<Clip> clip = repository_->find(segment.clipId);
			if (!clip) {
				*error = QStringLiteral("Segment clip not found");
				return false;
			}
			TimelineSegmentSource source;
			source.mediaPath = clip->mediaPath;
			source.sourceStartMs = segment.sourceStartMs;
			source.sourceEndMs = segment.sourceEndMs;
			source.gainDb = segment.gainDb;
			source.muted = segment.muted;
			request.segments.append(source);
		}
		request.progress = [this](double fraction) { emit exportProgress(fraction); };

		CancelCallback shouldCancel = [this]() { return cancelCurrent_.load(); };
		const TimelineExportResult result = FfmpegTimelineExporter().exportTimeline(request, shouldCancel);
		emit exportFinished(result.succeeded, result.cancelled, result.outputPath, result.error);
		if (!result.succeeded) {
			*error = result.error;
		}
		return result.succeeded;
	}

	*error = QStringLiteral("Unknown export job kind: %1").arg(kind);
	return false;
}

} // namespace MoonLit
