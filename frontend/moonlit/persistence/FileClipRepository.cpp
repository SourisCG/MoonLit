#include "FileClipRepository.hpp"

#include "ClipJson.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace MoonLit {
using namespace detail;

constexpr int kSchemaVersion = 1;

FileClipRepository::FileClipRepository(MoonLitPaths paths) : paths_(std::move(paths)) {}

bool FileClipRepository::open(QString *error)
{
	if (!paths_.ensureDirectories(error)) {
		return false;
	}

	if (!load(error)) {
		return false;
	}

	opened_ = true;
	return true;
}

bool FileClipRepository::reload(QString *error)
{
	if (!paths_.ensureDirectories(error)) {
		return false;
	}

	if (!load(error)) {
		return false;
	}

	opened_ = true;
	return true;
}

bool FileClipRepository::load(QString *error)
{
	QFile file(paths_.indexPath());
	if (!file.exists()) {
		clips_.clear();
		return true;
	}

	if (!file.open(QIODevice::ReadOnly)) {
		setError(error, QStringLiteral("Unable to read clip index: %1").arg(file.errorString()));
		return false;
	}

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		setError(error, QStringLiteral("Invalid clip index: %1").arg(parseError.errorString()));
		return false;
	}

	const QJsonObject root = document.object();
	const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(1);
	if (schemaVersion > kSchemaVersion) {
		setError(error, QStringLiteral("Unsupported clip index version: %1").arg(schemaVersion));
		return false;
	}

	QVector<Clip> loadedClips;
	const QJsonArray records = root.value(QStringLiteral("clips")).toArray();
	for (const QJsonValue &record : records) {
		if (!record.isObject()) {
			continue;
		}

		Clip clip;
		if (fromJson(record.toObject(), clip)) {
			loadedClips.append(std::move(clip));
		}
	}

	clips_ = std::move(loadedClips);
	return true;
}

bool FileClipRepository::save(QString *error) const
{
	QJsonArray records;
	for (const Clip &clip : clips_) {
		records.append(toJson(clip));
	}

	QJsonObject root;
	root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
	root.insert(QStringLiteral("clips"), records);

	// QSaveFile prevents a failed index write from replacing the last usable copy.
	QSaveFile file(paths_.indexPath());
	if (!file.open(QIODevice::WriteOnly)) {
		setError(error, QStringLiteral("Unable to write clip index: %1").arg(file.errorString()));
		return false;
	}

	const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
	if (file.write(data) != data.size() || !file.commit()) {
		setError(error, QStringLiteral("Unable to commit clip index: %1").arg(file.errorString()));
		return false;
	}

	return true;
}

bool FileClipRepository::ensureOpen(QString *error) const
{
	if (opened_) {
		return true;
	}

	setError(error, QStringLiteral("Clip repository is not open"));
	return false;
}

QVector<Clip> FileClipRepository::list(bool includeMissing) const
{
	QVector<Clip> result = clips_;
	if (!includeMissing) {
		result.erase(std::remove_if(result.begin(), result.end(),
					    [](const Clip &clip) { return clip.missing; }),
			     result.end());
	}

	std::sort(result.begin(), result.end(), [](const Clip &left, const Clip &right) {
		if (left.createdAtUtc != right.createdAtUtc) {
			return left.createdAtUtc > right.createdAtUtc;
		}
		return left.id < right.id;
	});
	return result;
}

QVector<Clip> FileClipRepository::search(const QString &query, bool includeMissing) const
{
	QVector<Clip> result;
	const QString trimmed = query.trimmed();
	for (const Clip &clip : clips_) {
		if (clip.missing && !includeMissing) {
			continue;
		}
		if (trimmed.isEmpty() || clip.title.contains(trimmed, Qt::CaseInsensitive) ||
		    clip.mediaPath.contains(trimmed, Qt::CaseInsensitive)) {
			result.append(clip);
		}
	}

	std::sort(result.begin(), result.end(), [](const Clip &left, const Clip &right) {
		if (left.createdAtUtc != right.createdAtUtc) {
			return left.createdAtUtc > right.createdAtUtc;
		}
		return left.id < right.id;
	});
	return result;
}

std::optional<Clip> FileClipRepository::find(const QString &id) const
{
	const auto iterator =
		std::find_if(clips_.cbegin(), clips_.cend(), [&id](const Clip &clip) { return clip.id == id; });
	if (iterator == clips_.cend()) {
		return std::nullopt;
	}

	return *iterator;
}

std::optional<Clip> FileClipRepository::findByMediaPath(const QString &mediaPath) const
{
	const auto iterator = std::find_if(clips_.cbegin(), clips_.cend(), [&mediaPath](const Clip &clip) {
		return samePath(clip.mediaPath, mediaPath);
	});
	if (iterator == clips_.cend()) {
		return std::nullopt;
	}

	return *iterator;
}

std::optional<Clip> FileClipRepository::upsert(Clip clip, QString *error)
{
	if (!ensureOpen(error)) {
		return std::nullopt;
	}
	if (clip.mediaPath.isEmpty()) {
		setError(error, QStringLiteral("A clip must have a media path"));
		return std::nullopt;
	}

	clip.mediaPath = normalizedPath(clip.mediaPath);
	if (clip.id.isEmpty()) {
		clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	}
	if (!clip.createdAtUtc.isValid()) {
		clip.createdAtUtc = QDateTime::currentDateTimeUtc();
	}

	int idIndex = -1;
	int pathIndex = -1;
	for (int index = 0; index < clips_.size(); ++index) {
		if (clips_.at(index).id == clip.id) {
			idIndex = index;
		}
		if (samePath(clips_.at(index).mediaPath, clip.mediaPath)) {
			pathIndex = index;
		}
	}
	if (idIndex >= 0 && pathIndex >= 0 && idIndex != pathIndex) {
		setError(error, QStringLiteral("Clip id and media path refer to different records"));
		return std::nullopt;
	}

	const int existingIndex = pathIndex >= 0 ? pathIndex : idIndex;

	if (existingIndex >= 0) {
		const Clip &existing = clips_.at(existingIndex);
		clip.id = existing.id;
		if (clip.title.isEmpty()) {
			clip.title = existing.title;
		}
		if (clip.thumbnailPath.isEmpty()) {
			clip.thumbnailPath = existing.thumbnailPath;
		}
	}
	if (clip.title.isEmpty()) {
		clip.title = QFileInfo(clip.mediaPath).completeBaseName();
	}
	if (clip.thumbnailPath.isEmpty()) {
		clip.thumbnailPath = paths_.thumbnailPath(clip.id);
	}

	refreshFileState(clip);
	const QVector<Clip> previousClips = clips_;
	if (existingIndex >= 0) {
		clips_[existingIndex] = clip;
	} else {
		clips_.append(clip);
	}

	if (!save(error)) {
		clips_ = previousClips;
		return std::nullopt;
	}

	return clip;
}

bool FileClipRepository::remove(const QString &id, QString *error)
{
	if (!ensureOpen(error)) {
		return false;
	}

	const auto iterator =
		std::find_if(clips_.cbegin(), clips_.cend(), [&id](const Clip &clip) { return clip.id == id; });
	if (iterator == clips_.cend()) {
		setError(error, QStringLiteral("Clip not found: %1").arg(id));
		return false;
	}

	const QVector<Clip> previousClips = clips_;
	clips_.erase(iterator);
	if (!save(error)) {
		clips_ = previousClips;
		return false;
	}

	return true;
}

bool FileClipRepository::reconcile(ReconcileSummary *summary, QString *error)
{
	if (!ensureOpen(error)) {
		return false;
	}

	ReconcileSummary result;
	const QVector<Clip> previousClips = clips_;
	bool changed = false;
	for (Clip &clip : clips_) {
		const bool wasMissing = clip.missing;
		const qint64 previousFileSize = clip.fileSize;
		const QDateTime previousFileModifiedAtUtc = clip.fileModifiedAtUtc;
		refreshFileState(clip);
		changed = changed || wasMissing != clip.missing || previousFileSize != clip.fileSize ||
			  previousFileModifiedAtUtc != clip.fileModifiedAtUtc;
		++result.scanned;
		if (!wasMissing && clip.missing) {
			++result.nowMissing;
		} else if (wasMissing && !clip.missing) {
			++result.restored;
		}
	}

	if (changed && !save(error)) {
		clips_ = previousClips;
		return false;
	}

	if (summary) {
		*summary = result;
	}
	return true;
}

} // namespace MoonLit
