#pragma once

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace MoonLit::exportmath {

/* Linear gain factor for a decibel value, clamped to [-60, 24] dB. */
inline double linearGainDb(double gainDb)
{
	return std::pow(10.0, std::clamp(gainDb, -60.0, 24.0) / 20.0);
}

/* A trim range is valid when the start is not negative and the end (if set)
 * is strictly after the start. */
inline bool isRangeValid(qint64 startMs, qint64 endMs)
{
	return startMs >= 0 && (endMs < 0 || endMs > startMs);
}

/* Duration the export should have for the requested range. A negative end
 * means "to the end of the source"; -1 is returned when it cannot be known. */
inline qint64 expectedDurationMs(qint64 startMs, qint64 endMs, qint64 sourceDurationMs)
{
	if (endMs >= 0) {
		return endMs - startMs;
	}
	if (sourceDurationMs < 0) {
		return -1;
	}
	return std::max<qint64>(0, sourceDurationMs - startMs);
}

/* Keyframe-aligned trim may start up to `startToleranceMs` earlier than the
 * requested point and end up to `endToleranceMs` later; anything outside that
 * is treated as a broken export. */
inline bool durationMatches(qint64 actualMs, qint64 expectedMs, qint64 startToleranceMs, qint64 endToleranceMs)
{
	if (expectedMs < 0 || actualMs < 0) {
		return false;
	}
	return actualMs >= expectedMs - startToleranceMs && actualMs <= expectedMs + endToleranceMs;
}

} // namespace MoonLit::exportmath
