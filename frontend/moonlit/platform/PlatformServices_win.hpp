#pragma once

#include "IPlatformServices.hpp"

namespace MoonLit {

/* Windows implementation: Explorer reveal, HKCU Run autostart, thread
 * priority via SetThreadPriority. */
class PlatformServices final : public IPlatformServices {
public:
	void revealInFileManager(const std::filesystem::path &path) override;
	void setLoginStartup(bool enabled) override;
	bool isLoginStartupEnabled() const override;
	void setWorkerThreadPriority(QThread *thread) override;
	const PlatformCapabilities &capabilities() const override;

private:
	PlatformCapabilities capabilities_{true, true};
};

} // namespace MoonLit
