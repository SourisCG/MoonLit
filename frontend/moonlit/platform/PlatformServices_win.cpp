#include "PlatformServices_win.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QSettings>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace MoonLit {

namespace {

QString nativePath(const std::filesystem::path &path)
{
	return QDir::toNativeSeparators(QString::fromStdWString(path.wstring()));
}

} // namespace

void PlatformServices::revealInFileManager(const std::filesystem::path &path)
{
	/* explorer.exe /select,<file> opens the parent folder with the file
	 * highlighted; a directory is revealed directly. */
	QProcess::startDetached(QStringLiteral("explorer.exe"),
				{QStringLiteral("/select,"), nativePath(path)});
}

void PlatformServices::setLoginStartup(bool enabled)
{
	QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
			   QSettings::NativeFormat);
	if (enabled) {
		const QString value = QStringLiteral("\"%1\" --minimize-to-tray")
					      .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
		settings.setValue(QStringLiteral("MoonLit"), value);
	} else {
		settings.remove(QStringLiteral("MoonLit"));
	}
}

bool PlatformServices::isLoginStartupEnabled() const
{
	QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
			   QSettings::NativeFormat);
	return settings.contains(QStringLiteral("MoonLit"));
}

void PlatformServices::setWorkerThreadPriority(QThread *thread)
{
	if (!thread) {
		return;
	}
	const HANDLE handle = reinterpret_cast<HANDLE>(thread->currentThreadId());
	if (handle) {
		SetThreadPriority(handle, THREAD_PRIORITY_BELOW_NORMAL);
	}
}

const PlatformCapabilities &PlatformServices::capabilities() const
{
	return capabilities_;
}

/* The factory is defined by whichever platform implementation is compiled. */
std::unique_ptr<IPlatformServices> IPlatformServices::create()
{
	return std::make_unique<PlatformServices>();
}

} // namespace MoonLit
