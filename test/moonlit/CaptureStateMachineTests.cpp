#include "MoonLitTest.hpp"

#include <moonlit/capture/CaptureStateMachine.hpp>

using namespace MoonLit;
using namespace MoonLitTest;

namespace {

CaptureTickInput idleInput()
{
	CaptureTickInput input;
	input.hasCaptureSource = false;
	input.targetValid = false;
	return input;
}

} // namespace

MOONLIT_TEST(capture_machine_idles_without_target)
{
	CaptureStateMachine machine;
	bool ok = expect(machine.decideTick(idleInput()) == TickAction::None, "idle does nothing", failure);
	return ok;
}

MOONLIT_TEST(capture_machine_configures_retry_on_missing_source)
{
	CaptureStateMachine machine;
	CaptureTickInput input = idleInput();
	input.targetValid = true;
	input.configureRetryElapsed = true;
	bool ok = expect(machine.decideTick(input) == TickAction::ConfigureRetry, "retries configure", failure);
	input.configureRetryElapsed = false;
	ok &= expect(machine.decideTick(input) == TickAction::None, "retry waits for the window", failure);
	return ok;
}

MOONLIT_TEST(capture_machine_starts_replay_on_wgc_first_frame)
{
	CaptureStateMachine machine;
	CaptureTickInput input = idleInput();
	input.hasCaptureSource = true;
	input.healthAvailable = true;
	input.healthActive = true;
	input.healthFirstFrame = true;
	input.healthWgc = true;
	bool ok = expect(machine.decideTick(input) == TickAction::StartReplay, "starts replay on first frame", failure);
	input.replayStartRequested = true;
	ok &= expect(machine.decideTick(input) == TickAction::WgcReady, "no double start while starting", failure);
	input.replayStartRequested = false;
	input.replayAutoBlocked = true;
	ok &= expect(machine.decideTick(input) == TickAction::WgcReady, "blocked replay does not start", failure);
	input.replayAutoBlocked = false;
	input.replayActive = true;
	ok &= expect(machine.decideTick(input) == TickAction::ResetReplayFailures, "active replay resets failures", failure);
	return ok;
}

MOONLIT_TEST(capture_machine_waits_then_falls_back_to_monitor)
{
	CaptureStateMachine machine;
	CaptureTickInput input = idleInput();
	input.hasCaptureSource = true;
	input.wgcElapsed5s = false;
	bool ok = expect(machine.decideTick(input) == TickAction::StatusInitializing, "initializing before 5 s", failure);
	input.wgcElapsed5s = true;
	input.monitorFallbackSafe = false;
	ok &= expect(machine.decideTick(input) == TickAction::FallbackBlocked, "unsafe fallback is blocked", failure);
	input.monitorFallbackSafe = true;
	ok &= expect(machine.decideTick(input) == TickAction::TryMonitorFallback, "safe fallback after 5 s", failure);
	return ok;
}

MOONLIT_TEST(capture_machine_handles_monitor_fallback_lifecycle)
{
	CaptureStateMachine machine;
	CaptureTickInput input = idleInput();
	input.hasCaptureSource = true;
	input.monitorFallback = true;
	input.fallbackReady = false;
	bool ok = expect(machine.decideTick(input) == TickAction::StatusInitializing, "fallback initializing", failure);
	input.fallbackReady = true;
	ok &= expect(machine.decideTick(input) == TickAction::StartReplay, "fallback starts replay", failure);
	input.replayActive = true;
	ok &= expect(machine.decideTick(input) == TickAction::MonitorFallbackReady, "fallback ready with replay", failure);
	input.replayActive = false;
	input.replayAutoBlocked = true;
	ok &= expect(machine.decideTick(input) == TickAction::MonitorFallbackReady, "fallback respects block", failure);
	return ok;
}

MOONLIT_TEST(capture_machine_ignores_tick_while_closing_or_unfocused)
{
	CaptureStateMachine machine;
	CaptureTickInput input = idleInput();
	input.closing = true;
	bool ok = expect(machine.decideTick(input) == TickAction::None, "closing stops ticking", failure);
	input.closing = false;
	input.focused = false;
	ok &= expect(machine.decideTick(input) == TickAction::None, "unfocused stops ticking", failure);
	return ok;
}

MOONLIT_TEST(capture_target_validity_rules)
{
	CaptureTarget target;
	bool ok = expect(!target.isValid(), "empty target is invalid", failure);
	target.processId = 1234;
	target.window = static_cast<uintptr_t>(0);
	ok &= expect(!target.isValid(), "zero handle is invalid", failure);
	target.window = static_cast<uintptr_t>(0x1234);
	ok &= expect(target.isValid(), "pid plus handle is valid", failure);
	target = CaptureTarget{};
	target.name = "wayland-app";
	target.window = std::string("org.freedesktop.some.App");
	ok &= expect(target.isValid(), "wayland app id is valid", failure);
	return ok;
}
