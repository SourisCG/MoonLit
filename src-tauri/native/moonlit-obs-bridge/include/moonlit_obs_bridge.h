#pragma once

#include <stddef.h>

#ifdef _WIN32
#define MOONLIT_OBS_API __declspec(dllexport)
#else
#define MOONLIT_OBS_API
#endif

// The bridge owns all libobs pointers. The Rust side receives only status
// values and completed-file callbacks through the sidecar protocol.
extern "C" {
MOONLIT_OBS_API int moonlit_obs_bridge_initialize(const char *runtime_root);
MOONLIT_OBS_API const char *moonlit_obs_bridge_last_error();
MOONLIT_OBS_API int moonlit_obs_bridge_probe_json(char *buffer, size_t capacity);
MOONLIT_OBS_API int moonlit_obs_bridge_start_json(const char *request_json, char *buffer,
                                                  size_t capacity);
MOONLIT_OBS_API int moonlit_obs_bridge_save_json(char *buffer, size_t capacity);
MOONLIT_OBS_API int moonlit_obs_bridge_stop_json();
MOONLIT_OBS_API void moonlit_obs_bridge_shutdown();
}
