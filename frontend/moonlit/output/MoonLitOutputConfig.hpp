#pragma once

#include <util/config-file.h>

namespace MoonLit {

/* One-time migration of a profile to MoonLit defaults: simple mode, MKV,
 * replay buffer enabled, four-track output and track names. An explicitly
 * saved encoder preference is preserved. Idempotent: guarded by the
 * "MoonLit/Migrated" flag in the profile. */
void MigrateProfileToMoonLitDefaults(config_t *config);

} /* namespace MoonLit */
