#include "Timeline.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>

namespace MoonLit {

namespace {

constexpr qint64 kMinSegmentMs = 100;

} // namespace

TimelineProject TimelineProject::create(const QString &name)
{
	TimelineProject project;
	project.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	project.name = name;
	return project;
}

qint64 TimelineProject::durationMs() const
{
	qint64 total = 0;
	for (const TimelineSegment &segment : segments) {
		if (segment.sourceEndMs >= 0) {
			total += segment.sourceLengthMs();
		}
	}
	return total;
}

bool TimelineProject::isValid(QString *reason) const
{
	if (id.isEmpty() || name.isEmpty()) {
		if (reason) {
			*reason = QStringLiteral("A timeline needs an id and a name");
		}
		return false;
	}
	if (segments.isEmpty()) {
		if (reason) {
			*reason = QStringLiteral("A timeline needs at least one segment");
		}
		return false;
	}
	for (const TimelineSegment &segment : segments) {
		if (segment.clipId.isEmpty()) {
			if (reason) {
				*reason = QStringLiteral("A segment needs a clip");
			}
			return false;
		}
		if (segment.sourceEndMs >= 0 && segment.sourceEndMs < segment.sourceStartMs + kMinSegmentMs) {
			if (reason) {
				*reason = QStringLiteral("Segments must be at least %1 ms long").arg(kMinSegmentMs);
			}
			return false;
		}
	}
	return true;
}

void TimelineProject::recomputePositions()
{
	qint64 position = 0;
	for (TimelineSegment &segment : segments) {
		segment.timelineStartMs = position;
		if (segment.sourceEndMs >= 0) {
			position += segment.sourceLengthMs();
		}
	}
}

QString timelineToJson(const TimelineProject &project)
{
	QJsonArray segments;
	for (const TimelineSegment &segment : project.segments) {
		QJsonObject object;
		object.insert(QStringLiteral("clipId"), segment.clipId);
		object.insert(QStringLiteral("startMs"), segment.sourceStartMs);
		object.insert(QStringLiteral("endMs"), segment.sourceEndMs);
		object.insert(QStringLiteral("posMs"), segment.timelineStartMs);
		object.insert(QStringLiteral("gainDb"), segment.gainDb);
		object.insert(QStringLiteral("muted"), segment.muted);
		segments.append(object);
	}

	QJsonObject root;
	root.insert(QStringLiteral("id"), project.id);
	root.insert(QStringLiteral("name"), project.name);
	root.insert(QStringLiteral("segments"), segments);
	return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool timelineFromJson(const QString &json, TimelineProject &project)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return false;
	}

	const QJsonObject root = document.object();
	TimelineProject parsed;
	parsed.id = root.value(QStringLiteral("id")).toString();
	parsed.name = root.value(QStringLiteral("name")).toString();
	for (const QJsonValue &value : root.value(QStringLiteral("segments")).toArray()) {
		if (!value.isObject()) {
			return false;
		}
		const QJsonObject object = value.toObject();
		TimelineSegment segment;
		segment.clipId = object.value(QStringLiteral("clipId")).toString();
		segment.sourceStartMs = object.value(QStringLiteral("startMs")).toInteger();
		segment.sourceEndMs = object.value(QStringLiteral("endMs")).toInteger(-1);
		segment.timelineStartMs = object.value(QStringLiteral("posMs")).toInteger();
		segment.gainDb = object.value(QStringLiteral("gainDb")).toDouble();
		segment.muted = object.value(QStringLiteral("muted")).toBool();
		parsed.segments.append(segment);
	}
	parsed.recomputePositions();
	project = parsed;
	return true;
}

} // namespace MoonLit
