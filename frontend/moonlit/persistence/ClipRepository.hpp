#pragma once

#include <moonlit/Clip.hpp>

#include <QVector>

#include <optional>

namespace MoonLit {

struct ReconcileSummary {
	int scanned{0};
	int nowMissing{0};
	int restored{0};
};

class ClipRepository {
public:
	virtual ~ClipRepository() = default;

	virtual bool open(QString *error = nullptr) = 0;
	virtual bool reload(QString *error = nullptr) = 0;
	virtual QVector<Clip> list(bool includeMissing = true) const = 0;
	virtual std::optional<Clip> find(const QString &id) const = 0;
	virtual std::optional<Clip> findByMediaPath(const QString &mediaPath) const = 0;
	virtual std::optional<Clip> upsert(Clip clip, QString *error = nullptr) = 0;
	virtual bool remove(const QString &id, QString *error = nullptr) = 0;
	virtual bool reconcile(ReconcileSummary *summary = nullptr, QString *error = nullptr) = 0;
};

} // namespace MoonLit
