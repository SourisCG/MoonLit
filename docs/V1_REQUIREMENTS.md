# MoonLit V1 Requirements

This file is the machine-readable working scope for the strict multimedia v1.
It does not replace external hardware, legal, signing, or clean-machine
evidence. A requirement is `verified` only when its implementation and its
required evidence exist for the same clean Git commit and artifact hash.

## Locked Scope

- Windows 10 Enterprise LTSC 2021 and Windows 11.
- Monitor and window capture through MoonLit-owned Windows Graphics Capture.
- H.264 and H.265/HEVC.
- MP4 and MKV for both codecs.
- NVENC, AMF, QuickSync, x264, and x265 where the required hardware/runtime is
  available.
- Replay buffers from 10 to 300 seconds, with 30 seconds as the default.
- System audio and microphone through WASAPI, including gain, mute, devices,
  AAC output, and A/V synchronization.
- Configurable save hotkey, tray, notifications, local storage, SQLite library,
  settings, secure playback, and H.264 HEVC preview proxy when required.
- Standard and offline Windows installers, exact runtime closure, SBOM,
  notices, corresponding source, and mandatory Authenticode signatures.

Application-specific audio, game detection, editor/export features, separated
tracks, overlays, updater, region capture, Linux, and MSIX are post-v1.

## Requirement IDs

| ID | Requirement | Required evidence | State |
|---|---|---|---|
| BASE-001 | Exact-commit baseline checks execute all production modules | Clean-SHA report, test inventory, CI run | in_progress |
| CONTRACT-001 | Capabilities are explicit encoder/codec/container tuples | Protocol and DTO tests | pending |
| CONTRACT-002 | Effective encoder, fallback, settings, and `canSave` are reported | Sidecar and host integration tests | pending |
| HOST-001 | Config migrations, recovery, validation, and transactional apply work | Host tests and restart fixtures | pending |
| HOST-002 | Storage cleanup and deletion cannot affect unrelated paths | Sentinel and traversal tests | pending |
| HOST-003 | SQLite migrations, pagination, reconciliation, and metadata are durable | 1,000-clip and restart tests | pending |
| RUNTIME-001 | Sidecar ABI, protocol, deadlines, parent death, and shutdown fail closed | Crash, hang, EOF, and lifecycle tests | pending |
| RUNTIME-002 | Runtime DLL closure is explicit and isolated from OBS/user PATH | Import and hostile-PATH report | pending |
| CAPTURE-001 | MoonLit-owned monitor WGC produces correctly timed frames | Windows hardware report | pending |
| CAPTURE-002 | Window WGC handles permission, resize, move, close, and source loss | Windows 10/11 report | pending |
| MEDIA-001 | x264 H.264 MP4 monitor vertical slice is real and decodable | ffprobe/decode/save report | pending |
| MEDIA-002 | H.264 MP4/MKV works with NVENC, AMF, QuickSync, and x264 | Per-tuple media evidence | pending |
| MEDIA-003 | HEVC MP4/MKV works with NVENC, AMF, QuickSync, and x265 | Per-tuple media evidence | pending |
| MEDIA-004 | No requested codec/container changes silently | Negative selection tests | pending |
| AUDIO-001 | WASAPI system and microphone capture/mixing works | Controlled-tone and device report | pending |
| AUDIO-002 | A/V drift is below 50 ms over one hour | Timestamp measurement report | pending |
| PROXY-001 | HEVC original is preserved and required H.264 proxy plays | Hash, ffprobe, WebView2 report | pending |
| PRODUCT-001 | UI, F8, tray, and host save use one reliable action | Hidden-WebView and E2E tests | pending |
| PRODUCT-002 | Hotkey, notifications, lifecycle settings, and errors are truthful | Host and UI tests | pending |
| LIBRARY-001 | Library search, tags, favorites, status, thumbnails, and playback work | UI and SQLite tests | pending |
| RELEASE-001 | Runtime, licenses, notices, SBOM, and source offer match closure | Legal/security review | blocked |
| RELEASE-002 | Signed standard and offline NSIS installers work cleanly | Clean-machine reports | blocked |
| QUAL-001 | NVIDIA, AMD, Intel, and CPU-only matrix passes | Hardware reports | blocked |
| QUAL-002 | 100 saves, 50 lifecycle cycles, and 24-hour encoder soaks pass | Soak reports | blocked |

## Evidence Rules

- FakeBackend, raw Annex-B, a compile, and a probe cannot satisfy a real media
  requirement.
- Every report must include full Git SHA, worktree status, OS build, hardware,
  tool versions, commands, exit codes, artifact hashes, and timestamps.
- Missing AMD, Intel, CPU-only, Windows 10, certificate, or legal resources are
  blockers for release, not passes.
