#include "MoonLitPaths.hpp"

#include <QDir>
#include <QStandardPaths>

#include <utility>

namespace MoonLit {
namespace {

QString safeComponent(const QString &value)
{
	QString result;
	for (const QChar character : value) {
		if (character.isLetterOrNumber() || character == QLatin1Char('-') || character == QLatin1Char('_')) {
			result.append(character);
		} else {
			result.append(QLatin1Char('_'));
		}
	}

	return result.isEmpty() ? QStringLiteral("clip") : result;
}

bool makeDirectory(const QString &path, QString *error)
{
	if (QDir().mkpath(path)) {
		return true;
	}

	if (error) {
		*error = QStringLiteral("Unable to create directory: %1").arg(path);
	}
	return false;
}

} // namespace

MoonLitPaths::MoonLitPaths(QString rootPath) : rootPath_(QDir::cleanPath(std::move(rootPath))) {}

MoonLitPaths MoonLitPaths::defaultPaths()
{
	QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if (root.isEmpty()) {
		root = QDir::homePath() + QStringLiteral("/.moonlit");
	}

	return MoonLitPaths(std::move(root));
}

const QString &MoonLitPaths::rootPath() const
{
	return rootPath_;
}

QString MoonLitPaths::clipsPath() const
{
	return QDir(rootPath_).filePath(QStringLiteral("clips"));
}

QString MoonLitPaths::indexPath() const
{
	return QDir(clipsPath()).filePath(QStringLiteral("index.json"));
}

QString MoonLitPaths::thumbnailsPath() const
{
	return QDir(rootPath_).filePath(QStringLiteral("thumbnails"));
}

QString MoonLitPaths::exportsPath() const
{
	return QDir(rootPath_).filePath(QStringLiteral("exports"));
}

QString MoonLitPaths::temporaryPath() const
{
	return QDir(rootPath_).filePath(QStringLiteral("tmp"));
}

QString MoonLitPaths::thumbnailPath(const QString &clipId) const
{
	return QDir(thumbnailsPath()).filePath(safeComponent(clipId) + QStringLiteral(".png"));
}

QString MoonLitPaths::exportPath(const QString &clipId, const QString &extension) const
{
	QString safeExtension = safeComponent(extension);
	if (safeExtension.startsWith(QLatin1Char('_'))) {
		safeExtension.remove(0, 1);
	}
	if (safeExtension.isEmpty()) {
		safeExtension = QStringLiteral("mkv");
	}

	return QDir(exportsPath()).filePath(safeComponent(clipId) + QLatin1Char('.') + safeExtension);
}

bool MoonLitPaths::ensureDirectories(QString *error) const
{
	return makeDirectory(rootPath_, error) && makeDirectory(clipsPath(), error) &&
	       makeDirectory(thumbnailsPath(), error) && makeDirectory(exportsPath(), error) &&
	       makeDirectory(temporaryPath(), error);
}

} // namespace MoonLit
