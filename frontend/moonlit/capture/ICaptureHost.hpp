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
	virtual void startReplayBuffer() = 0;
	virtual void stopReplayBuffer() = 0;
	/* The active configuration (profile) holding MoonLit settings. */
	virtual config_t *activeConfig() = 0;
};

} // namespace MoonLit
