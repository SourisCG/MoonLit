#pragma once

#include "IPlatformServices.hpp"

namespace MoonLit {

/* Linux implementation: file manager reveal via xdg-open, XDG autostart
 * desktop entry, thread priority via setpriority(2). Compiles on any Linux
 * toolchain and runs on both X11 and Wayland sessions. */
class PlatformServices final : public IPlatformServices {
public:
	void revealInFileManager(const std::filesystem::path &path) override;
	void setLoginStartup(bool enabled) override;
	bool isLoginStartupEnabled() const override;
	void setWorkerThreadPriority(QThread *thread) override;
	const PlatformCapabilities &capabilities() const override;

private:
	PlatformCapabilities capabilities_{false, false};
};

} // namespace MoonLit
