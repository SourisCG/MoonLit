#pragma once

#include "ClipRepository.hpp"

#include <moonlit/MoonLitPaths.hpp>

#include <optional>

namespace MoonLit {

class FileClipRepository final : public ClipRepository {
public:
	explicit FileClipRepository(MoonLitPaths paths);
	~FileClipRepository() override = default;

	FileClipRepository(const FileClipRepository &) = delete;
	FileClipRepository &operator=(const FileClipRepository &) = delete;
	FileClipRepository(FileClipRepository &&) = delete;
	FileClipRepository &operator=(FileClipRepository &&) = delete;

	bool open(QString *error = nullptr) override;
	bool reload(QString *error = nullptr) override;
	QVector<Clip> list(bool includeMissing = true) const override;
	std::optional<Clip> find(const QString &id) const override;
	std::optional<Clip> findByMediaPath(const QString &mediaPath) const override;
	std::optional<Clip> upsert(Clip clip, QString *error = nullptr) override;
	bool remove(const QString &id, QString *error = nullptr) override;
	bool reconcile(ReconcileSummary *summary = nullptr, QString *error = nullptr) override;

private:
	bool load(QString *error);
	bool save(QString *error) const;
	bool ensureOpen(QString *error) const;

	MoonLitPaths paths_;
	QVector<Clip> clips_;
	bool opened_{false};
};

} // namespace MoonLit
