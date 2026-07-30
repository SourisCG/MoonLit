#include "moonlit_obs_bridge.h"

#include <cstring>

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

namespace {
int copy_json(const char *json, char *buffer, size_t capacity) {
  const size_t length = std::strlen(json);
  if (buffer == nullptr || capacity <= length) {
    return -1;
  }
  std::memcpy(buffer, json, length + 1);
  return static_cast<int>(length);
}

int unavailable_json(char *buffer, size_t capacity) {
  return copy_json(
      "{\"type\":\"error\",\"data\":{\"code\":\"backendUnavailable\",\"message\":\"MoonLit WGC bridge is not implemented in this build\",\"retryable\":true}}",
      buffer, capacity);
}
} // namespace

extern "C" int moonlit_obs_bridge_probe_json(char *buffer, size_t capacity) {
  return copy_json(
      "{\"available\":false,\"sources\":[],\"encoders\":[],\"maxWidth\":null,\"maxHeight\":null,\"maxFps\":null,\"note\":\"MoonLit WGC bridge is not implemented in this build\",\"codecs\":[\"h264\",\"hevc\"],\"formats\":[\"mp4\",\"mkv\"],\"audio\":{\"available\":false,\"systemAudio\":false,\"microphone\":false,\"applicationAudio\":false,\"note\":\"WASAPI bridge is not implemented in this build\"}}",
      buffer, capacity);
}

extern "C" int moonlit_obs_bridge_start_json(const char *request_json, char *buffer,
                                               size_t capacity) {
  (void)request_json;
  return unavailable_json(buffer, capacity);
}

extern "C" int moonlit_obs_bridge_save_json(char *buffer, size_t capacity) {
  return unavailable_json(buffer, capacity);
}

extern "C" int moonlit_obs_bridge_stop_json() { return 0; }

extern "C" void moonlit_obs_bridge_shutdown() {}
