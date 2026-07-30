#include "moonlit_obs_bridge.h"

#include <obs.h>

namespace {
const char *last_error =
    "MoonLit libobs source registration is not enabled in this build";
}

extern "C" int moonlit_obs_bridge_initialize(const char *runtime_root) {
  (void)runtime_root;
  // Keep the current bridge fail-closed until the WGC source and explicit
  // module allowlist are implemented against the pinned libobs headers.
  if (obs_initialized()) {
    last_error = "MoonLit libobs bridge is missing its WGC source implementation";
  }
  return -1;
}

extern "C" const char *moonlit_obs_bridge_last_error() { return last_error; }

extern "C" void moonlit_obs_bridge_shutdown() {}
