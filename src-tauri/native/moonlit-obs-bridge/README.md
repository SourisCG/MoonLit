# MoonLit libobs bridge

This directory reserves the narrow C/C++ ABI boundary used by
`moonlit-recorder`. It is deliberately not linked into the Tauri process.

The bridge is not release-ready yet. Its implementation must be built against
the exact OBS commit recorded in `packaging/windows/obs-runtime.lock.json` and
must provide:

* absolute module/data path setup;
* explicit allowlisted module loading;
* host-owned WGC monitor/window source registration through `libobs-winrt`;
* replay output setup and saved-file callbacks;
* reverse-order shutdown before `obs_shutdown`.

Do not copy OBS Studio's `win-capture` plugin into the runtime. It contains
Game Capture and graphics-hook/injection infrastructure prohibited by MoonLit.
