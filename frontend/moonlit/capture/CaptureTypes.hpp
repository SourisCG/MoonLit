#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace MoonLit {

/* Capture backend families. Each maps to a libobs source family that already
 * exists on the platform: WGC/DXGI (Windows), PipeWire/XSHM (Linux). */
enum class BackendKind { Wgc, DxgiMonitor, PipeWire, Xshm };

/* Platform-neutral window identity. Windows carries an HWND (uintptr_t),
 * X11 a window id, Wayland a wl_surface pointer plus an app_id string. */
using WindowHandle = std::variant<uintptr_t, void *, std::string>;

struct CaptureTarget {
	std::string name;
	std::string windowClass;
	std::string executablePath;
	uint64_t processId = 0;
	uint64_t creationTimeNs = 0;
	WindowHandle window;

	bool isValid() const;
};

struct CaptureHealth {
	bool active = false;
	bool firstFrameReceived = false;
	bool shielding = false;
	BackendKind activeKind = BackendKind::Wgc;
};

} // namespace MoonLit
