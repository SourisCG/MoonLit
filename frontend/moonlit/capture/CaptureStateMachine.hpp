#pragma once

#include "CaptureTypes.hpp"

namespace MoonLit {

/* Decisions the capture health tick can emit. The controller executes them;
 * this machine only reasons about state, so the fallback policy is fully
 * testable without OBS. */
enum class TickAction {
	None,
	ConfigureRetry, /* capture source missing, target known, retry window open */
	StatusInitializing,
	WgcReady, /* first WGC frame delivered: reveal and report */
	StartReplay,
	ResetReplayFailures,
	MonitorFallbackReady,
	FallbackBlocked,    /* window does not cover its monitor */
	TryMonitorFallback, /* 5 s without a WGC frame, fallback is safe */
};

struct CaptureTickInput {
	bool closing = false;
	bool focused = true;
	bool hasCaptureSource = false;
	bool targetValid = false;
	bool configureRetryElapsed = true; /* timer invalid or elapsed >= 1000 ms */
	bool monitorFallback = false;
	bool fallbackReady = false; /* source has nonzero dimensions */
	bool healthAvailable = false;
	bool healthActive = false;
	bool healthFirstFrame = false;
	bool healthWgc = false;
	bool replayActive = false;
	bool replayStartRequested = false;
	bool replayAutoBlocked = false;
	bool replayRetryElapsed = true; /* timer invalid or elapsed >= 1000 ms */
	bool wgcElapsed5s = true;       /* timer invalid or elapsed >= 5000 ms */
	bool monitorFallbackSafe = true;
};

/* Pure decision engine for the capture health tick. Faithful mirror of the
 * MoonLit capture state flow: WGC first frame -> unshield + replay start,
 * then DXGI monitor fallback after 5 s when the window covers its monitor. */
class CaptureStateMachine {
public:
	explicit CaptureStateMachine(BackendKind preferred = BackendKind::Wgc);

	TickAction decideTick(const CaptureTickInput &input) const;

	BackendKind preferred() const { return preferred_; }

private:
	BackendKind preferred_;
};

} // namespace MoonLit
