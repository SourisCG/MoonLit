#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

namespace MoonLit {

/* Pure path matching against the user's remembered game list
 * (MoonLit.GameList). Case-insensitive; entries ending with a slash match
 * every executable under that directory. Header-only so the capture core
 * and its unit tests can use it without the Windows process helpers. */
inline bool matchesGameListPath(const QString &executablePath, const QStringList &gameList)
{
	const QString path = QDir::fromNativeSeparators(executablePath).trimmed().toLower();
	if (path.isEmpty()) {
		return false;
	}
	for (const QString &entry : gameList) {
		const QString candidate = QDir::fromNativeSeparators(entry).trimmed().toLower();
		if (candidate.isEmpty()) {
			continue;
		}
		if (candidate.endsWith(QChar('/'))) {
			if (path.startsWith(candidate)) {
				return true;
			}
			continue;
		}
		if (path == candidate) {
			return true;
		}
	}
	return false;
}

} // namespace MoonLit
