#pragma once

#include <moonlit/Clip.hpp>

#include <QString>

#include <optional>

namespace MoonLit {

class MediaProbe {
public:
	virtual ~MediaProbe() = default;

	virtual std::optional<ClipMetadata> probe(const QString &mediaPath, QString *error = nullptr) const = 0;
};

class FfmpegMediaProbe final : public MediaProbe {
public:
	std::optional<ClipMetadata> probe(const QString &mediaPath, QString *error = nullptr) const override;
};

} // namespace MoonLit
