#pragma once

#include <QString>

#include <functional>

namespace MoonLit {

enum class ClipExportMode {
	FastKeyframeAligned,
};

using ProgressCallback = std::function<void(double)>;

struct ClipExportRequest {
	QString sourcePath;
	QString destinationPath;
	qint64 startMs{0};
	qint64 endMs{-1};
	bool muted{false};
	double gainDb{0.0};
	ClipExportMode mode{ClipExportMode::FastKeyframeAligned};
	ProgressCallback progress;
};

struct ClipExportResult {
	bool succeeded{false};
	bool cancelled{false};
	qint64 durationMs{-1};
	QString outputPath;
	QString error;
};

using CancelCallback = std::function<bool()>;

class ClipExportService {
public:
	virtual ~ClipExportService() = default;

	virtual ClipExportResult exportClip(const ClipExportRequest &request,
					    CancelCallback shouldCancel = {}) const = 0;
};

class FfmpegClipExportService final : public ClipExportService {
public:
	ClipExportResult exportClip(const ClipExportRequest &request, CancelCallback shouldCancel = {}) const override;
};

} // namespace MoonLit
