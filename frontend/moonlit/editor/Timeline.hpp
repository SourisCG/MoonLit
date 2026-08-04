#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

namespace MoonLit {

/* One entry on the MoonLit timeline: a range inside an existing clip,
 * positioned by the cumulative length of the previous segments. */
struct TimelineSegment {
	QString clipId;
	qint64 sourceStartMs = 0;
	qint64 sourceEndMs = -1; /* -1 = to the end of the clip */
	qint64 timelineStartMs = 0;
	double gainDb = 0.0;
	bool muted = false;

	qint64 sourceLengthMs() const { return sourceEndMs - sourceStartMs; }
};

/* A named, ordered collection of segments (single track). Positions are
 * derived: segment i starts where segment i-1 ends. */
struct TimelineProject {
	QString id; /* UUID */
	QString name;
	QVector<TimelineSegment> segments;

	static TimelineProject create(const QString &name);

	qint64 durationMs() const;
	bool isValid(QString *reason = nullptr) const;
	void recomputePositions();
};

QString timelineToJson(const TimelineProject &project);
bool timelineFromJson(const QString &json, TimelineProject &project);

} // namespace MoonLit

Q_DECLARE_METATYPE(MoonLit::TimelineProject)
