#pragma once

#include <moonlit/capture/CaptureTypes.hpp>

#include <obs.hpp>

namespace MoonLit {

/* Platform capture backend. Windows today (WGC window capture with DXGI
 * monitor fallback); PipeWire/XSHM will implement the same surface. The
 * backend owns the OBS sources and scene items of the private capture graph:
 * the capture source, the black privacy shield and the four audio sources. */
class ICaptureBackend {
public:
	virtual ~ICaptureBackend() = default;

	virtual BackendKind kind() const = 0;

	/* Creates the window capture source, the audio graph and the shield for
	 * the given target. Returns false when any essential piece failed. */
	virtual bool attachWindow(const CaptureTarget &target) = 0;

	/* Swaps the capture source for a full-monitor DXGI capture. */
	virtual bool attachMonitorFallback(const CaptureTarget &target) = 0;

	/* Full-screen mode: captures the whole primary monitor directly, with
	 * no window target, no shield and no game process audio. */
	virtual bool attachFullscreen() = 0;

	virtual bool hasCapture() const = 0;
	virtual bool hasVideo() const = 0;
	virtual CaptureHealth health() const = 0;

	/* Shield choreography: shield() hides the capture behind opaque black,
	 * cover() shows the shield above the capture, reveal() unshields. */
	virtual void shield() = 0;
	virtual void cover() = 0;
	virtual void reveal() = 0;

	/* Removes every source and scene item owned by the backend. */
	virtual void detach() = 0;

	virtual void setProcessAudioEnabled(bool enabled) = 0;
	virtual void applyNoiseSuppression() = 0;

	virtual bool monitorFallbackIsSafe(const CaptureTarget &target) const = 0;

	/* Mixer sources; invalid sources are null. */
	virtual OBSSource desktopSource() const = 0;
	virtual OBSSource gameSource() const = 0;
	virtual OBSSource micSource() const = 0;
	virtual OBSSource chatSource() const = 0;
};

} // namespace MoonLit
