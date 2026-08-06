#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

namespace MoonLit {

namespace detail {

/* Comparison key for a Windows-style game path: lowercase with both
 * separator flavours folded to '/', so "D:\\Games\\g.exe" and
 * "d:/games/g.exe" compare equal on any platform. */
inline QString gameListKey(const QString &value)
{
	QString key = QDir::fromNativeSeparators(value).trimmed().toLower();
	key.replace(QChar('\\'), QChar('/'));
	return key;
}

} // namespace detail

/* Pure path matching against the user's remembered game list
 * (MoonLit.GameList). Case-insensitive; entries ending with a slash match
 * every executable under that directory. Header-only so the capture core
 * and its unit tests can use it without the Windows process helpers. */
inline bool matchesGameListPath(const QString &executablePath, const QStringList &gameList)
{
	const QString path = detail::gameListKey(executablePath);
	if (path.isEmpty()) {
		return false;
	}
	for (const QString &entry : gameList) {
		const QString candidate = detail::gameListKey(entry);
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
