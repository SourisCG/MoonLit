#include "MoonLitTest.hpp"

#include <moonlit/editor/ExportMath.hpp>

using namespace MoonLit::exportmath;
using namespace MoonLitTest;

MOONLIT_TEST(exportmath_rejects_invalid_ranges)
{
	bool ok = expect(!isRangeValid(-1, 1000), "negative start is invalid", failure);
	ok &= expect(!isRangeValid(0, 0), "zero-length range is invalid", failure);
	ok &= expect(!isRangeValid(500, 500), "empty range is invalid", failure);
	ok &= expect(!isRangeValid(1000, 500), "reversed range is invalid", failure);
	ok &= expect(isRangeValid(0, -1), "full-length export is valid", failure);
	ok &= expect(isRangeValid(500, 1000), "trim range is valid", failure);
	return ok;
}

MOONLIT_TEST(exportmath_computes_expected_duration)
{
	bool ok = expect(expectedDurationMs(500, 1000, 4000) == 500, "fixed range uses end minus start", failure);
	ok &= expect(expectedDurationMs(0, -1, 4000) == 4000, "open end uses the source duration", failure);
	ok &= expect(expectedDurationMs(500, -1, 4000) == 3500, "open end subtracts the start", failure);
	ok &= expect(expectedDurationMs(500, -1, -1) == -1, "unknown source duration is unknown", failure);
	ok &= expect(expectedDurationMs(0, -1, 0) == 0, "empty source has zero duration", failure);
	ok &= expect(expectedDurationMs(6000, -1, 4000) == 0, "start past the end clamps to zero", failure);
	return ok;
}

MOONLIT_TEST(exportmath_duration_tolerance)
{
	/* Ten-second clip with 500 ms start and 10 s end tolerance. */
	constexpr qint64 expected = 10000;
	constexpr qint64 startTolerance = 500;
	constexpr qint64 endTolerance = 10000;

	bool ok = expect(durationMatches(expected, expected, startTolerance, endTolerance),
			 "exact match passes", failure);
	ok &= expect(durationMatches(9800, expected, startTolerance, endTolerance),
		     "small shorter drift passes", failure);
	ok &= expect(durationMatches(11500, expected, startTolerance, endTolerance),
		     "keyframe-aligned longer output passes", failure);
	ok &= expect(durationMatches(expected + endTolerance, expected, startTolerance, endTolerance),
		     "near upper tolerance passes", failure);
	ok &= expect(durationMatches(expected - startTolerance, expected, startTolerance, endTolerance),
		     "near lower tolerance passes", failure);
	ok &= expect(!durationMatches(9000, expected, startTolerance, endTolerance), "far too short fails", failure);
	ok &= expect(!durationMatches(30000, expected, startTolerance, endTolerance), "far too long fails", failure);
	ok &= expect(!durationMatches(-1, expected, startTolerance, endTolerance), "unknown actual duration fails",
		     failure);
	ok &= expect(!durationMatches(expected, -1, startTolerance, endTolerance), "unknown expected duration fails",
		     failure);
	return ok;
}
