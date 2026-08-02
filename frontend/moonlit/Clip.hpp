#pragma once

#include <QDateTime>
#include <QString>

namespace MoonLit {

struct ClipMetadata {
	qint64 durationMs{-1};
	int width{0};
	int height{0};
	double frameRate{0.0};
	qint64 bitRate{0};
	int videoStreamCount{0};
	int audioStreamCount{0};
	bool hasAudio{false};
	QString container;
	QString videoCodec;
	QString audioCodec;
};

struct Clip {
	QString id;
	QString title;
	QString mediaPath;
	QString thumbnailPath;
	QDateTime createdAtUtc;
	ClipMetadata metadata;
	qint64 fileSize{-1};
	QDateTime fileModifiedAtUtc;
	qint64 trimStartMs{0};
	qint64 trimEndMs{-1};
	double gainDb{0.0};
	bool muted{false};
	bool missing{false};

	static Clip create(const QString &mediaPath, const QString &title = {});

	bool isValid() const;
	bool hasEdits() const;
	qint64 effectiveEndMs() const;
};

} // namespace MoonLit
