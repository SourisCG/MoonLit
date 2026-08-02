#include "Clip.hpp"

#include <QFileInfo>
#include <QUuid>

namespace MoonLit {

Clip Clip::create(const QString &mediaPath, const QString &title)
{
	const QFileInfo fileInfo(mediaPath);
	Clip clip;
	clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	clip.mediaPath = fileInfo.absoluteFilePath();
	clip.title = title.isEmpty() ? fileInfo.completeBaseName() : title;
	clip.createdAtUtc = QDateTime::currentDateTimeUtc();
	clip.missing = !fileInfo.exists() || !fileInfo.isFile();
	if (!clip.missing) {
		clip.fileSize = fileInfo.size();
		clip.fileModifiedAtUtc = fileInfo.lastModified().toUTC();
	}
	return clip;
}

bool Clip::isValid() const
{
	return !id.isEmpty() && !mediaPath.isEmpty() && trimStartMs >= 0 && (trimEndMs < 0 || trimEndMs > trimStartMs);
}

bool Clip::hasEdits() const
{
	return trimStartMs > 0 || trimEndMs >= 0 || muted || gainDb != 0.0;
}

qint64 Clip::effectiveEndMs() const
{
	if (trimEndMs >= 0) {
		return trimEndMs;
	}

	return metadata.durationMs;
}

} // namespace MoonLit
