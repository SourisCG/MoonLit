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
#endif

#ifdef _WIN32
static bool isFullscreenWindow(HWND window)
{
	if (!window || !IsWindow(window) || IsIconic(window) || !IsWindowVisible(window)) {
		return false;
	}
	const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
	if (!monitor) {
		return false;
	}
	MONITORINFO info = {};
	info.cbSize = sizeof(info);
	RECT windowRect = {};
	if (!GetMonitorInfoW(monitor, &info) || !GetWindowRect(window, &windowRect)) {
		return false;
	}
	return EqualRect(&info.rcMonitor, &windowRect);
}
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
	const bool candidateValid = foreground && MoonLit::WindowsProcessUtil::readWindowTarget(
							     reinterpret_cast<quintptr>(foreground), candidate) &&
					     isLikelyGame(candidate);

	if (activeTarget_.isValid()) {
		MoonLitTarget current;
		const bool identityValid = MoonLit::WindowsProcessUtil::readWindowTarget(activeTarget_.window, current) &&
					    sameIdentity(activeTarget_, current) &&
					    MoonLit::WindowsProcessUtil::processAlive(activeTarget_);
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

bool MoonLitGameDetector::sameIdentity(const MoonLitTarget &left, const MoonLitTarget &right)
{
	return left.isValid() && right.isValid() && left.window == right.window && left.processId == right.processId &&
		left.creationTime == right.creationTime;
}

bool MoonLitGameDetector::isLikelyGame(const MoonLitTarget &target) const
{
#ifdef _WIN32
	const QString path = target.executablePath.toLower();
	static const QStringList knownLauncherPaths = {
		QStringLiteral("\\steamapps\\common\\"),
		QStringLiteral("\\steamapps\\"),
		QStringLiteral("\\epic games\\"),
		QStringLiteral("\\gog galaxy\\games\\"),
		QStringLiteral("\\battle.net\\"),
		QStringLiteral("\\world of warcraft\\"),
		QStringLiteral("\\riot games\\"),
		QStringLiteral("\\ubisoft game launcher\\"),
		QStringLiteral("\\ubisoft connect\\"),
		QStringLiteral("\\ea games\\"),
		QStringLiteral("\\ea app\\"),
		QStringLiteral("\\windowsapps\\"),
		QStringLiteral("\\itch.io\\apps\\"),
		QStringLiteral("\\games\\"),
	};
	for (const QString &fragment : knownLauncherPaths) {
		if (path.contains(fragment)) {
			return true;
		}
	}
	if (MoonLit::WindowsProcessUtil::matchesManualGameList(target.executablePath, gameList_)) {
		return true;
	}
	/* A foreground window covering a full monitor is treated as a game:
	 * covers launcher-less installs, emulators and exclusive-fullscreen
	 * titles that do not live under a known launcher directory. */
	return isFullscreenWindow(reinterpret_cast<HWND>(target.window));
#else
	Q_UNUSED(target);
	return false;
#endif
}
