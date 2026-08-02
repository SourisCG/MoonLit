/******************************************************************************
    MoonLit foreground game detector

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
******************************************************************************/

#include "MoonLitGameDetector.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>

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

} // namespace
#endif

MoonLitGameDetector::MoonLitGameDetector(QObject *parent) : QObject(parent)
{
	monotonicTimer_.start();
	timer_.setInterval(500);
	timer_.setSingleShot(false);
	timer_.setTimerType(Qt::CoarseTimer);
	timer_.setParent(this);
	connect(&timer_, &QTimer::timeout, this, &MoonLitGameDetector::poll);
}

void MoonLitGameDetector::start()
{
	if (!timer_.isActive()) {
		timer_.start();
	}
	poll();
}

void MoonLitGameDetector::stop()
{
	timer_.stop();
	pendingTarget_ = {};
	pendingSinceMs_ = 0;
	targetFocused_ = false;
	if (activeTarget_.isValid()) {
		activeTarget_ = {};
		emit targetLost();
	}
}

void MoonLitGameDetector::poll()
{
#ifdef _WIN32
	const qint64 now = monotonicTimer_.elapsed();
	const HWND foreground = GetAncestor(GetForegroundWindow(), GA_ROOT);
	MoonLitTarget candidate;
	const bool candidateValid = foreground && readTarget(reinterpret_cast<quintptr>(foreground), candidate) &&
					    isLikelyGame(candidate);

	if (activeTarget_.isValid()) {
		MoonLitTarget current;
		const bool identityValid = readTarget(activeTarget_.window, current) && sameIdentity(activeTarget_, current) &&
					isProcessAlive(activeTarget_);
		if (!identityValid) {
			if (targetFocused_) {
				targetFocused_ = false;
				emit targetFocusChanged(false);
			}

			/* A game can replace its top-level window while the process exits. If
			 * the replacement is already foreground and stable, rebind directly
			 * instead of briefly accepting an unrelated window. */
			if (candidateValid && !sameIdentity(activeTarget_, candidate)) {
				if (!sameIdentity(pendingTarget_, candidate)) {
					pendingTarget_ = candidate;
					pendingSinceMs_ = now;
					return;
				}

				if (now - pendingSinceMs_ < 1500) {
					return;
				}

				activeTarget_ = candidate;
				pendingTarget_ = {};
				pendingSinceMs_ = 0;
				targetFocused_ = true;
				emit targetDetected(activeTarget_);
				return;
			}

			activeTarget_ = {};
			pendingTarget_ = {};
			pendingSinceMs_ = 0;
			targetFocused_ = false;
			emit targetLost();
			return;
		}

		const bool focused = foreground && reinterpret_cast<quintptr>(foreground) == activeTarget_.window;
		if (focused != targetFocused_) {
			targetFocused_ = focused;
			emit targetFocusChanged(focused);
		}
		return;
	}

	if (!candidateValid) {
		pendingTarget_ = {};
		pendingSinceMs_ = 0;
		return;
	}

	if (!sameIdentity(pendingTarget_, candidate)) {
		pendingTarget_ = candidate;
		pendingSinceMs_ = now;
		return;
	}

	if (now - pendingSinceMs_ < 1500) {
		return;
	}

	activeTarget_ = candidate;
	pendingTarget_ = {};
	pendingSinceMs_ = 0;
	targetFocused_ = true;
	emit targetDetected(activeTarget_);
#else
	/* The detector intentionally remains inert until a platform backend is
	 * added for Linux. */
#endif
}

bool MoonLitGameDetector::readTarget(quintptr window, MoonLitTarget &target)
{
#ifdef _WIN32
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
#else
	Q_UNUSED(window);
	Q_UNUSED(target);
	return false;
#endif
}

bool MoonLitGameDetector::sameIdentity(const MoonLitTarget &left, const MoonLitTarget &right)
{
	return left.isValid() && right.isValid() && left.window == right.window && left.processId == right.processId &&
		left.creationTime == right.creationTime;
}

bool MoonLitGameDetector::isLikelyGame(const MoonLitTarget &target)
{
#ifdef _WIN32
	const QString path = target.executablePath.toLower();
	return path.contains(QStringLiteral("\\steamapps\\common\\")) ||
	       path.contains(QStringLiteral("\\epic games\\")) || path.contains(QStringLiteral("\\gog galaxy\\games\\")) ||
	       path.contains(QStringLiteral("\\games\\"));
#else
	Q_UNUSED(target);
	return false;
#endif
}

bool MoonLitGameDetector::isProcessAlive(const MoonLitTarget &target)
{
#ifdef _WIN32
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
#else
	Q_UNUSED(target);
	return false;
#endif
}
