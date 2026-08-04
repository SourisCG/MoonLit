#pragma once

#include <QThread>
#include <QString>

#include <filesystem>
#include <memory>

namespace MoonLit {

struct PlatformCapabilities {
	bool processLoopbackAudio = false;
	bool secureDesktopHandling = false;
};

/* Operating-system services used by the MoonLit core. The implementation is
 * selected by CMake (platform/windows.cpp or platform/linux.cpp), so the core
 * itself never carries platform conditionals. */
class IPlatformServices {
public:
	virtual ~IPlatformServices() = default;

	static std::unique_ptr<IPlatformServices> create();

	/* Selects the file in the system file manager. */
	virtual void revealInFileManager(const std::filesystem::path &path) = 0;

	/* Registers or removes the per-user login startup entry. */
	virtual void setLoginStartup(bool enabled) = 0;
	virtual bool isLoginStartupEnabled() const = 0;

	/* Lowers the given thread below normal priority for background work. */
	virtual void setWorkerThreadPriority(QThread *thread) = 0;

	virtual const PlatformCapabilities &capabilities() const = 0;
};

} // namespace MoonLit
