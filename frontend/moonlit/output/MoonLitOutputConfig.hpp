#pragma once

#include <util/config-file.h>

#include <QString>

namespace MoonLit {

/* MoonLit's own default recording folder: outside the folders protected by
 * Windows' Controlled Folder Access (Desktop, Documents, Pictures, Videos,
 * Music, Favorites), so Defender never blocks clip saves. Created on
 * demand. The user can change it in Ajustes at any time. */
QString DefaultRecordingFolder();

/* One-time migration of a profile to MoonLit defaults: simple mode, MKV,
 * replay buffer enabled, four-track output and track names. An explicitly
 * saved encoder preference is preserved. Idempotent: guarded by the
 * "MoonLit/Migrated" flag in the profile. The recording folder is migrated
 * to MoonLit's own folder once (MoonLit.FolderMigrated), treating the stock
 * OBS default (Videos) as "not chosen". */
void MigrateProfileToMoonLitDefaults(config_t *config);

} /* namespace MoonLit */
