#pragma once

#include <QImage>
#include <QString>
#include <QVector>

namespace MoonLit {

/* Decodes still frames with FFmpeg for the clip editor: a single frame at an
 * exact position and a contact sheet of evenly spaced frames. Like the
 * thumbnail service it must run on the worker thread, never on the UI. */
class ClipPreviewService {
public:
	ClipPreviewService() = default;

	ClipPreviewService(const ClipPreviewService &) = delete;
	ClipPreviewService &operator=(const ClipPreviewService &) = delete;

	QImage frameAt(const QString &mediaPath, qint64 positionMs, int maximumWidth = 640,
		       QString *error = nullptr) const;
	QVector<QImage> frameStrip(const QString &mediaPath, int count, int maximumWidth = 160,
				   QString *error = nullptr) const;
};

} // namespace MoonLit
