#pragma once

#include "WindowsTarget.hpp"

#include <QStringList>
#include <QVector>

namespace MoonLit {

/* Windows process/window helpers shared by the game detector, the manual
 * game picker and the capture controller. All functions are no-ops (or
 * return "nothing") off Windows. */
namespace WindowsProcessUtil {

/* Reads the identity of a top-level window (HWND, pid, creation time,
 * title, class, executable path). False when the window is not a usable
 * top-level window or belongs to an ignored executable. */
bool readWindowTarget(quintptr window, MoonLitTarget &target);

/* True while the target's window still exists and belongs to the same
 * process instance (pid + creation time). */
bool processAlive(const MoonLitTarget &target);

/* Enumerates visible top-level windows with a title, deduplicated by
 * process, excluding ignored executables. Used by the manual picker. */
QVector<MoonLitTarget> enumerateTopLevelTargets();

/* Executables that are never captured (shell, search, MoonLit itself...). */
bool isIgnoredExecutable(const QString &executable);

/* Pure path matching against the user's remembered game list
 * (MoonLit.GameList). Case-insensitive, trailing-slash tolerant. */
bool matchesManualGameList(const QString &executablePath, const QStringList &gameList);

} // namespace WindowsProcessUtil

} // namespace MoonLit
