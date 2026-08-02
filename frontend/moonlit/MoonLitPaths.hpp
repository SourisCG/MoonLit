#pragma once

#include <QString>

namespace MoonLit {

class MoonLitPaths {
public:
	explicit MoonLitPaths(QString rootPath);

	static MoonLitPaths defaultPaths();

	/* Data root next to a portable install: <app dir>/../MoonLitData when the
	 * `portable_mode` marker sits next to the executable, empty otherwise. */
	static QString portableDataRoot(const QString &applicationDir);

	const QString &rootPath() const;
	QString clipsPath() const;
	QString indexPath() const;
	QString databasePath() const;
	QString thumbnailsPath() const;
	QString exportsPath() const;
	QString temporaryPath() const;
	QString thumbnailPath(const QString &clipId) const;
	QString exportPath(const QString &clipId, const QString &extension = QStringLiteral("mkv")) const;

	bool ensureDirectories(QString *error = nullptr) const;

private:
	QString rootPath_;
};

} // namespace MoonLit
