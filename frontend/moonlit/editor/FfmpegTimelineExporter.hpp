#pragma once

#include <QString>
#include <QVector>

#include <functional>

namespace MoonLit {

using CancelCallback = std::function<bool()>;

struct TimelineSegmentSource {
	QString mediaPath;
	qint64 sourceStartMs = 0;
	qint64 sourceEndMs = -1; /* -1 = to the end of the media */
	double gainDb = 0.0;
	bool muted = false;
};

struct TimelineExportRequest {
	QString destinationPath;
	QVector<TimelineSegmentSource> segments;
	std::function<void(double)> progress;
};

struct TimelineExportResult {
	bool succeeded = false;
	bool cancelled = false;
	qint64 durationMs = -1;
	QString outputPath;
	QString error;
};

/* Exports a multi-clip timeline by decoding every segment and encoding a
 * single h264 + aac output, so cuts between files are clean (stream-copy
 * concatenation is unreliable across GOPs). Runs entirely on the caller's
 * thread; cancellation is cooperative through the callback. */
class FfmpegTimelineExporter {
public:
	TimelineExportResult exportTimeline(const TimelineExportRequest &request, CancelCallback shouldCancel) const;
};

} // namespace MoonLit
