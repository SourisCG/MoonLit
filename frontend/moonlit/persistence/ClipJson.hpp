#pragma once

#include <moonlit/Clip.hpp>

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

namespace MoonLit::detail {

inline QString normalizedPath(const QString &path)
{
	const QFileInfo fileInfo(path);
	const QString canonicalPath = fileInfo.canonicalFilePath();
	return QDir::cleanPath(canonicalPath.isEmpty() ? fileInfo.absoluteFilePath() : canonicalPath);
}

inline bool samePath(const QString &left, const QString &right)
{
	const QString normalizedLeft = normalizedPath(left);
	const QString normalizedRight = normalizedPath(right);
#ifdef Q_OS_WIN
	return QString::compare(normalizedLeft, normalizedRight, Qt::CaseInsensitive) == 0;
#else
	return normalizedLeft == normalizedRight;
#endif
}

inline void refreshFileState(Clip &clip)
{
	const QFileInfo fileInfo(clip.mediaPath);
	clip.missing = !fileInfo.exists() || !fileInfo.isFile();
	if (clip.missing) {
		clip.fileSize = -1;
		clip.fileModifiedAtUtc = {};
		return;
	}

	clip.fileSize = fileInfo.size();
	clip.fileModifiedAtUtc = fileInfo.lastModified().toUTC();
}

inline void setError(QString *error, const QString &message)
{
	if (error) {
		*error = message;
	}
}

inline QJsonObject toJson(const Clip &clip)
{
	QJsonObject metadata;
	metadata.insert(QStringLiteral("durationMs"), clip.metadata.durationMs);
	metadata.insert(QStringLiteral("width"), clip.metadata.width);
	metadata.insert(QStringLiteral("height"), clip.metadata.height);
	metadata.insert(QStringLiteral("frameRate"), clip.metadata.frameRate);
	metadata.insert(QStringLiteral("bitRate"), clip.metadata.bitRate);
	metadata.insert(QStringLiteral("videoStreamCount"), clip.metadata.videoStreamCount);
	metadata.insert(QStringLiteral("audioStreamCount"), clip.metadata.audioStreamCount);
	metadata.insert(QStringLiteral("hasAudio"), clip.metadata.hasAudio);
	metadata.insert(QStringLiteral("container"), clip.metadata.container);
	metadata.insert(QStringLiteral("videoCodec"), clip.metadata.videoCodec);
	metadata.insert(QStringLiteral("audioCodec"), clip.metadata.audioCodec);

	QJsonObject object;
	object.insert(QStringLiteral("id"), clip.id);
	object.insert(QStringLiteral("title"), clip.title);
	object.insert(QStringLiteral("mediaPath"), clip.mediaPath);
	object.insert(QStringLiteral("thumbnailPath"), clip.thumbnailPath);
	object.insert(QStringLiteral("createdAtUtc"), clip.createdAtUtc.toUTC().toString(Qt::ISODateWithMs));
	object.insert(QStringLiteral("fileSize"), clip.fileSize);
	object.insert(QStringLiteral("fileModifiedAtUtc"), clip.fileModifiedAtUtc.toUTC().toString(Qt::ISODateWithMs));
	object.insert(QStringLiteral("trimStartMs"), clip.trimStartMs);
	object.insert(QStringLiteral("trimEndMs"), clip.trimEndMs);
	object.insert(QStringLiteral("gainDb"), clip.gainDb);
	object.insert(QStringLiteral("muted"), clip.muted);
	object.insert(QStringLiteral("metadata"), metadata);
	return object;
}

inline bool fromJson(const QJsonObject &object, Clip &clip)
{
	clip.id = object.value(QStringLiteral("id")).toString();
	clip.mediaPath = object.value(QStringLiteral("mediaPath")).toString();
	if (clip.id.isEmpty() || clip.mediaPath.isEmpty()) {
		return false;
	}

	clip.title = object.value(QStringLiteral("title")).toString();
	clip.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
	clip.createdAtUtc =
		QDateTime::fromString(object.value(QStringLiteral("createdAtUtc")).toString(), Qt::ISODateWithMs)
			.toUTC();
	clip.fileSize = object.value(QStringLiteral("fileSize")).toInteger(-1);
	clip.fileModifiedAtUtc =
		QDateTime::fromString(object.value(QStringLiteral("fileModifiedAtUtc")).toString(), Qt::ISODateWithMs)
			.toUTC();
	clip.trimStartMs = object.value(QStringLiteral("trimStartMs")).toInteger(0);
	clip.trimEndMs = object.value(QStringLiteral("trimEndMs")).toInteger(-1);
	clip.gainDb = object.value(QStringLiteral("gainDb")).toDouble(0.0);
	clip.muted = object.value(QStringLiteral("muted")).toBool(false);

	const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
	clip.metadata.durationMs = metadata.value(QStringLiteral("durationMs")).toInteger(-1);
	clip.metadata.width = metadata.value(QStringLiteral("width")).toInt(0);
	clip.metadata.height = metadata.value(QStringLiteral("height")).toInt(0);
	clip.metadata.frameRate = metadata.value(QStringLiteral("frameRate")).toDouble(0.0);
	clip.metadata.bitRate = metadata.value(QStringLiteral("bitRate")).toInteger(0);
	clip.metadata.videoStreamCount = metadata.value(QStringLiteral("videoStreamCount")).toInt(0);
	clip.metadata.audioStreamCount = metadata.value(QStringLiteral("audioStreamCount")).toInt(0);
	clip.metadata.hasAudio = metadata.value(QStringLiteral("hasAudio")).toBool(false);
	clip.metadata.container = metadata.value(QStringLiteral("container")).toString();
	clip.metadata.videoCodec = metadata.value(QStringLiteral("videoCodec")).toString();
	clip.metadata.audioCodec = metadata.value(QStringLiteral("audioCodec")).toString();

	clip.mediaPath = normalizedPath(clip.mediaPath);
	refreshFileState(clip);
	return clip.isValid();
}

} // namespace MoonLit::detail
