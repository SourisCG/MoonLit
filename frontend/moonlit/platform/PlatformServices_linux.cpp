#include "PlatformServices_linux.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#include <cerrno>
#include <sys/resource.h>

namespace MoonLit {

namespace {

std::filesystem::path autostartEntryPath()
{
	const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
	const QString directory = config.isEmpty() ? QDir::homePath() + QStringLiteral("/.config")
						   : config;
	return std::filesystem::path(directory.toStdString()) / "autostart" / "moonlit.desktop";
}

} // namespace

void PlatformServices::revealInFileManager(const std::filesystem::path &path)
{
	/* xdg-open cannot highlight a file; reveal its parent directory. */
	const std::filesystem::path directory = std::filesystem::is_directory(path) ? path : path.parent_path();
	QProcess::startDetached(QStringLiteral("xdg-open"), {QString::fromStdString(directory.string())});
}

void PlatformServices::setLoginStartup(bool enabled)
{
	const std::filesystem::path entry = autostartEntryPath();
	if (!enabled) {
		std::filesystem::remove(entry);
		return;
	}

	std::error_code error;
	std::filesystem::create_directories(entry.parent_path(), error);

	QFile file(QString::fromStdString(entry.string()));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return;
	}
	QTextStream stream(&file);
	stream << "[Desktop Entry]\n"
	       << "Type=Application\n"
	       << "Name=MoonLit\n"
	       << "Exec=" << QCoreApplication::applicationFilePath() << " --minimize-to-tray\n"
	       << "X-GNOME-Autostart-enabled=true\n";
}

bool PlatformServices::isLoginStartupEnabled() const
{
	return std::filesystem::exists(autostartEntryPath());
}

void PlatformServices::setWorkerThreadPriority(QThread *thread)
{
	Q_UNUSED(thread)
	/* Background work runs below normal priority via nice(10). */
	errno = 0;
	if (setpriority(PRIO_PROCESS, 0, 10) == -1 && errno != 0) {
		/* Ignore permission errors: best effort only. */
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
