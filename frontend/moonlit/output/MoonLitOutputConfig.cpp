#include "MoonLitOutputConfig.hpp"

#include <string.h>

namespace MoonLit {

static void SetStringIfAbsent(config_t *config, const char *section, const char *key, const char *value)
{
	if (!config_has_user_value(config, section, key))
		config_set_string(config, section, key, value);
}

void MigrateProfileToMoonLitDefaults(config_t *config)
{
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
