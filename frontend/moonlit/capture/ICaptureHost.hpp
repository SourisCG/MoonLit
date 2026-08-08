#pragma once

#include <obs.hpp>
#include <util/config-file.h>

namespace MoonLit {

/* Bridge from the capture controller to the OBSBasic host. Keeping this as an
 * interface lets the controller be tested and ported without depending on the
 * OBS frontend window. */
class ICaptureHost {
public:
	virtual ~ICaptureHost() = default;

	virtual bool isClosing() const = 0;
	virtual OBSScene moonlitCurrentScene() = 0;
	virtual bool replayBufferActive() = 0;
	/* silent: automatic (detector-driven) starts/stops — no modal dialogs.
	 * startReplayBuffer returns the synchronous result of the output start
	 * so the controller can schedule retries without racing the async
	 * OBS_OUTPUT_STARTED event. */
	virtual bool startReplayBuffer(bool silent = false) = 0;
	virtual void stopReplayBuffer(bool silent = false) = 0;
	/* The active configuration (profile) holding MoonLit settings. */
	virtual config_t *activeConfig() = 0;
};

} // namespace MoonLit
