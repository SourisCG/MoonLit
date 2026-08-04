#include "MoonLitTest.hpp"

#include <moonlit/editor/Timeline.hpp>
#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/persistence/SqliteClipRepository.hpp>

#include <QDir>
#include <QTemporaryDir>

using namespace MoonLit;
using namespace MoonLitTest;

namespace {

TimelineProject sampleProject()
{
	TimelineProject project = TimelineProject::create(QStringLiteral("Compilation"));
	TimelineSegment first;
	first.clipId = QStringLiteral("clip-a");
	first.sourceStartMs = 0;
	first.sourceEndMs = 30000;
	TimelineSegment second;
	second.clipId = QStringLiteral("clip-b");
	second.sourceStartMs = 5000;
	second.sourceEndMs = 15000;
	project.segments.append(first);
	project.segments.append(second);
	project.recomputePositions();
	return project;
}

} // namespace

MOONLIT_TEST(timeline_model_positions_and_duration)
{
	TimelineProject project = sampleProject();
	bool ok = expect(project.durationMs() == 40000, "duration is the sum of segment lengths", failure);
	ok &= expect(project.segments[0].timelineStartMs == 0, "first segment starts at zero", failure);
	ok &= expect(project.segments[1].timelineStartMs == 30000, "second segment follows the first", failure);
	ok &= expect(project.isValid(), "sample project is valid", failure);
	return ok;
}

MOONLIT_TEST(timeline_model_validates_segments)
{
	TimelineProject project = TimelineProject::create(QStringLiteral("Broken"));
	QString reason;
	bool ok = expect(!project.isValid(&reason), "empty timeline is invalid", failure);
	ok &= expect(reason.contains(QStringLiteral("segment")), "reason mentions segments", failure);

	TimelineSegment tooShort;
	tooShort.clipId = QStringLiteral("clip-a");
	tooShort.sourceStartMs = 0;
	tooShort.sourceEndMs = 50;
	project.segments.append(tooShort);
	ok &= expect(!project.isValid(&reason), "sub-100ms segment is invalid", failure);
	return ok;
}

MOONLIT_TEST(timeline_json_round_trip)
{
	TimelineProject project = sampleProject();
	const QString json = timelineToJson(project);
	TimelineProject parsed;
	bool ok = expect(timelineFromJson(json, parsed), "json parses", failure);
	ok &= expect(parsed.id == project.id && parsed.name == project.name, "identity survives json", failure);
	ok &= expect(parsed.segments.size() == 2, "segments survive json", failure);
	ok &= expect(parsed.segments[1].clipId == QStringLiteral("clip-b") &&
			     parsed.segments[1].sourceStartMs == 5000 && parsed.segments[1].muted == false,
		     "segment fields survive json", failure);
	ok &= expect(parsed.segments[1].timelineStartMs == 30000, "positions recomputed after load", failure);
	ok &= expect(timelineFromJson(QStringLiteral("{not json"), parsed) == false, "invalid json rejected", failure);
	return ok;
}

MOONLIT_TEST(timeline_repository_round_trip)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;
	SqliteClipRepository repository(paths);
	if (!repository.open(&error)) {
		*failure = QStringLiteral("open failed: %1").arg(error);
		return false;
	}

	TimelineProject project = sampleProject();
	bool ok = expect(repository.saveTimeline(project, &error), "timeline saves", failure);

	QVector<TimelineProject> listed = repository.listTimelines(&error);
	ok &= expect(listed.size() == 1 && listed.first().id == project.id, "timeline lists", failure);

	const auto loaded = repository.loadTimeline(project.id, &error);
	ok &= expect(loaded.has_value() && loaded->segments.size() == 2, "timeline loads", failure);
	if (loaded) {
		ok &= expect(loaded->segments[1].sourceEndMs == 15000, "loaded segment range intact", failure);
	}

	project.name = QStringLiteral("Renamed");
	ok &= expect(repository.saveTimeline(project, &error), "timeline updates", failure);
	ok &= expect(repository.listTimelines(&error).size() == 1, "update does not duplicate", failure);

	ok &= expect(repository.deleteTimeline(project.id, &error), "timeline deletes", failure);
	ok &= expect(repository.listTimelines(&error).isEmpty(), "list is empty after delete", failure);
	return ok;
}
