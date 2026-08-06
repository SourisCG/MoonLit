#include "MoonLitOutputConfig.hpp"

#include <QDir>
#include <QStandardPaths>

#include <string.h>

namespace MoonLit {

QString DefaultRecordingFolder()
{
	const QString folder =
		QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::HomeLocation) +
				QStringLiteral("/MoonLit/Clips"));
	QDir().mkpath(folder);
	return folder;
}

static void SetStringIfAbsent(config_t *config, const char *section, const char *key, const char *value)
{
	if (!config_has_user_value(config, section, key))
		config_set_string(config, section, key, value);
}

static QString normalizePath(const QString &path)
{
	return QDir::fromNativeSeparators(path).trimmed().toLower();
}

void MigrateProfileToMoonLitDefaults(config_t *config)
{
	/* Recording folder migration (one-time): when the configured path is
	 * empty or still the stock OBS default (the Videos known folder), point
	 * it at MoonLit's own folder. Any folder the user picks explicitly
	 * afterwards — Videos included — is always preserved. */
	if (!config_get_bool(config, "MoonLit", "FolderMigrated")) {
		const QString ownFolder = DefaultRecordingFolder();
		const QString videosFolder =
			normalizePath(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
		const char *folder = config_get_string(config, "SimpleOutput", "FilePath");
		const QString configuredPath = normalizePath(QString::fromUtf8(folder ? folder : ""));
		if (configuredPath.isEmpty() || configuredPath == videosFolder) {
			config_set_string(config, "SimpleOutput", "FilePath", ownFolder.toUtf8().constData());
		}
		config_set_bool(config, "MoonLit", "FolderMigrated", true);
	}

	if (config_get_bool(config, "MoonLit", "Migrated"))
		return;

	config_set_string(config, "Output", "Mode", "Simple");
	config_set_string(config, "SimpleOutput", "RecFormat2", "mkv");
	config_set_bool(config, "SimpleOutput", "RecRB", true);
	config_set_int(config, "SimpleOutput", "RecRBTime", 20);
	config_set_uint(config, "SimpleOutput", "RecTracks", (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3));
	config_set_uint(config, "AdvOut", "RecTracks", (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3));
	config_set_bool(config, "Video", "AutoRemux", true);

	/* Never overwrite an explicitly saved encoder preference. */
	SetStringIfAbsent(config, "SimpleOutput", "StreamEncoder", "nvenc");
	SetStringIfAbsent(config, "SimpleOutput", "RecEncoder", "nvenc");

	SetStringIfAbsent(config, "AdvOut", "Track1Name", "Mixed");
	SetStringIfAbsent(config, "AdvOut", "Track2Name", "Game");
	SetStringIfAbsent(config, "AdvOut", "Track3Name", "Microphone");
	SetStringIfAbsent(config, "AdvOut", "Track4Name", "Chat");

	config_set_bool(config, "MoonLit", "Migrated", true);
	config_save_safe(config, "tmp", nullptr);
}

} /* namespace MoonLit */
