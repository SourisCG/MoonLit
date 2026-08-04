#include "CaptureStateMachine.hpp"

namespace MoonLit {

CaptureStateMachine::CaptureStateMachine(BackendKind preferred) : preferred_(preferred) {}

TickAction CaptureStateMachine::decideTick(const CaptureTickInput &input) const
{
	if (input.closing || !input.focused) {
		return TickAction::None;
	}

	if (!input.hasCaptureSource) {
		if (input.targetValid && input.configureRetryElapsed) {
			return TickAction::ConfigureRetry;
		}
		return TickAction::None;
	}

	if (input.monitorFallback) {
		if (!input.fallbackReady) {
			return TickAction::StatusInitializing;
		}
		if (!input.replayActive && !input.replayStartRequested && !input.replayAutoBlocked &&
		    input.replayRetryElapsed) {
			return TickAction::StartReplay;
		}
		return TickAction::MonitorFallbackReady;
	}

	if (input.healthAvailable && input.healthActive && input.healthFirstFrame && input.healthWgc) {
		if (!input.replayActive && !input.replayStartRequested && !input.replayAutoBlocked &&
		    input.replayRetryElapsed) {
			return TickAction::StartReplay;
		}
		if (input.replayActive) {
			return TickAction::ResetReplayFailures;
		}
		return TickAction::WgcReady;
	}

	if (!input.wgcElapsed5s) {
		return TickAction::StatusInitializing;
	}

	if (!input.monitorFallbackSafe) {
		return TickAction::FallbackBlocked;
	}

	return TickAction::TryMonitorFallback;
}

} // namespace MoonLit
