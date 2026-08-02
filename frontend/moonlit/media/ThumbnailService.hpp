#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <optional>

namespace MoonLit {

struct ThumbnailOptions {
	QSize maximumSize{320, 180};
	qint64 timestampMs{0};
};

class ThumbnailService {
public:
	virtual ~ThumbnailService() = default;

	virtual std::optional<QImage> frameAt(const QString &mediaPath, const ThumbnailOptions &options = {},
					      QString *error = nullptr) const = 0;
	virtual bool writeThumbnail(const QString &mediaPath, const QString &thumbnailPath,
				    const ThumbnailOptions &options = {}, QString *error = nullptr) const = 0;
};

class FfmpegThumbnailService final : public ThumbnailService {
public:
	std::optional<QImage> frameAt(const QString &mediaPath, const ThumbnailOptions &options = {},
				      QString *error = nullptr) const override;
	bool writeThumbnail(const QString &mediaPath, const QString &thumbnailPath,
			    const ThumbnailOptions &options = {}, QString *error = nullptr) const override;
};

} // namespace MoonLit
