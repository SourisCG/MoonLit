#include "WindowsProcessUtil.hpp"

#include "GameListMatch.hpp"

#include <QDir>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <iterator>
#include <set>
#endif

namespace MoonLit {
namespace WindowsProcessUtil {

#ifdef _WIN32

namespace {

QString encodeName(const wchar_t *value)
{
	return QString::fromWCharArray(value);
}

QString processPath(HANDLE process)
{
	wchar_t path[32768] = {};
	DWORD length = static_cast<DWORD>(std::size(path));
	if (!QueryFullProcessImageNameW(process, 0, path, &length)) {
		return {};
	}
	return QString::fromWCharArray(path, static_cast<int>(length));
}

quint64 fileTimeValue(const FILETIME &time)
{
	ULARGE_INTEGER value;
	value.LowPart = time.dwLowDateTime;
	value.HighPart = time.dwHighDateTime;
	return value.QuadPart;
}

} // namespace

bool readWindowTarget(quintptr window, MoonLitTarget &target)
{
	const HWND hwnd = reinterpret_cast<HWND>(window);
	if (!IsWindow(hwnd)) {
		return false;
	}
	if (GetAncestor(hwnd, GA_ROOT) != hwnd) {
		return false;
	}

	DWORD processId = 0;
	if (!GetWindowThreadProcessId(hwnd, &processId) || processId == 0 || processId == GetCurrentProcessId()) {
		return false;
	}

	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, processId);
	if (!process) {
		return false;
	}

	FILETIME creationTime = {}, exitTime = {}, kernelTime = {}, userTime = {};
	const bool timesRead = GetProcessTimes(process, &creationTime, &exitTime, &kernelTime, &userTime) != FALSE &&
				WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
	const QString path = timesRead ? processPath(process) : QString();
	CloseHandle(process);
	if (!timesRead || path.isEmpty()) {
		return false;
	}

	wchar_t title[512] = {};
	wchar_t windowClass[256] = {};
	GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
	GetClassNameW(hwnd, windowClass, static_cast<int>(std::size(windowClass)));

	target.window = window;
	target.processId = processId;
	target.creationTime = fileTimeValue(creationTime);
	target.title = encodeName(title);
	target.windowClass = encodeName(windowClass);
	target.executablePath = path;
	target.executable = path.section(QChar('\\'), -1);
	return !target.title.isEmpty() && !isIgnoredExecutable(target.executable);
}

bool processAlive(const MoonLitTarget &target)
{
	const HWND hwnd = reinterpret_cast<HWND>(target.window);
	if (!target.isValid() || !IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) {
		return false;
	}

	DWORD windowProcessId = 0;
	if (!GetWindowThreadProcessId(hwnd, &windowProcessId) || windowProcessId != target.processId) {
		return false;
	}

	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, target.processId);
	if (!process) {
		return false;
	}

	FILETIME creationTime = {}, exitTime = {}, kernelTime = {}, userTime = {};
	const bool alive = GetProcessTimes(process, &creationTime, &exitTime, &kernelTime, &userTime) != FALSE &&
				fileTimeValue(creationTime) == target.creationTime &&
				WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
	CloseHandle(process);
	return alive;
}

namespace {

BOOL CALLBACK collectTopLevel(HWND hwnd, LPARAM param)
{
	auto *targets = reinterpret_cast<QVector<MoonLitTarget> *>(param);
	wchar_t title[512] = {};
	GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
	if (title[0] == L'\0' || !IsWindowVisible(hwnd)) {
		return TRUE;
	}

	MoonLitTarget target;
	if (readWindowTarget(reinterpret_cast<quintptr>(hwnd), target)) {
		targets->append(target);
	}
	return TRUE;
}

} // namespace

QVector<MoonLitTarget> enumerateTopLevelTargets()
{
	QVector<MoonLitTarget> targets;
	EnumWindows(collectTopLevel, reinterpret_cast<LPARAM>(&targets));

	/* Deduplicate by process: keep the first (most relevant) window. */
	std::set<quint32> seen;
	targets.erase(std::remove_if(targets.begin(), targets.end(),
				     [&seen](const MoonLitTarget &target) {
					     return !seen.insert(target.processId).second;
				     }),
		      targets.end());
	return targets;
}

bool isIgnoredExecutable(const QString &executable)
{
	static const QStringList ignored = {
		QStringLiteral("explorer.exe"),
		QStringLiteral("searchhost.exe"),
		QStringLiteral("startmenuexperiencehost.exe"),
		QStringLiteral("textinputhost.exe"),
		QStringLiteral("applicationframehost.exe"),
		QStringLiteral("systemsettings.exe"),
		QStringLiteral("taskmgr.exe"),
		QStringLiteral("dwm.exe"),
		QStringLiteral("sihost.exe"),
		QStringLiteral("runtimebroker.exe"),
		QStringLiteral("moonlit.exe"),
		QStringLiteral("obs64.exe"),
		QStringLiteral("obs.exe"),
	};
	return ignored.contains(executable.toLower());
}

#endif

bool matchesManualGameList(const QString &executablePath, const QStringList &gameList)
{
	return matchesGameListPath(executablePath, gameList);
}

} // namespace WindowsProcessUtil

} // namespace MoonLit
