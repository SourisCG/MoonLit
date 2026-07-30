# Windows Packaging

## Status

NSIS is the planned Windows distribution format, but release bundling is not
enabled in `src-tauri/tauri.conf.json`. The release-only configuration is
`src-tauri/tauri.windows.release.conf.json` and must not be used until the
libobs runtime lock has status `approved`.

Ordinary development remains FakeBackend-first and does not require OBS,
FFmpeg or a staged recorder.

## Installed Layout

The installer places application-local runtime resources below the Tauri
resource directory:

```text
MoonLit/
  MoonLit.exe
  runtime/obs/
    runtime-manifest.json
    THIRD_PARTY_NOTICES.txt
    bin/64bit/
      moonlit-recorder.exe
      moonlit-obs-bridge.dll
      obs.dll
      libobs-d3d11.dll
      libobs-winrt.dll
      obs-ffmpeg-mux.exe
      <allowlisted dependency DLLs>
    obs-plugins/64bit/
      <allowlisted modules only>
    data/libobs/
    data/obs-plugins/
    licenses/
```

The recorder is a resource rather than a Tauri `externalBin`. This preserves
its DLL/plugin/data closure and avoids flattening the OBS runtime beside the
main executable. Trusted Rust code launches the absolute path directly; the
frontend receives no shell permission.

## Runtime Inputs

The current design lock is `packaging/windows/obs-runtime.lock.json`:

* OBS Studio `32.2.1`, commit `0052d024fd6a5ff1aa04c76cbdffd3085a5dfacc`.
* Source archive and portable reference SHA-256 values are recorded there.
* The lock is currently `design-only`; it is not a release input.
* The release pipeline must reject `latest`, mutable unverified archives,
  missing hashes and non-approved manifests.

`runtime.allowlist.json` is authoritative. The denylist is defense in depth
and includes `win-capture`, graphics hooks, injectors, Game Capture helpers,
OBS Studio, browser, websocket and virtual-camera files.

The staging process must reject unknown files, wrong PE architecture, duplicate
DLL basenames, reparse points, archive traversal, unresolved imports and
components without license records. GPU driver DLLs such as
`nvEncodeAPI64.dll` are not bundled.

## Build Order

1. Require a clean, exact Git commit and locked Rust/npm dependencies.
2. Fetch the exact OBS source/dependency archives and verify SHA-256.
3. Build only the no-frontend libobs targets and the MoonLit recorder/bridge.
4. Recreate `target/package-stage/windows-x86_64` from empty.
5. Run `packaging/windows/Stage-Runtime.ps1` with the approved manifests.
6. Inspect PE architecture/import closure and run recorder `--self-test --json`.
7. Generate the unsigned runtime manifest and SBOM.
8. Sign the staged MoonLit recorder, bridge and runtime DLLs in a protected
   signing environment.
9. Generate the signed manifest and include license/source notices.
10. Build NSIS with the release configuration and verify the installed tree.

The build scripts must invoke tools with executable paths and argument arrays.
They must not use `Invoke-Expression`, `cmd /c`, user-provided command strings,
`build.rs` downloads or runtime downloads.

## WebView2

The primary installer uses Tauri's `downloadBootstrapper` mode. A future
offline installer may use WebView2's offline installer and will be a separate
artifact because of its size. MoonLit does not use a fixed WebView2 runtime.

## Launch Rules

Release code resolves exactly:

```text
resource_dir/runtime/obs/bin/64bit/moonlit-recorder.exe
```

It does not search `PATH`, Program Files OBS, the registry, current directory,
or user plugin directories. The sidecar receives `--stdio`, an absolute
runtime root and the parent PID. The process supervisor enforces request
deadlines, drains stderr into a bounded ring, kills/reaps on timeout and
detects EOF/nonzero exit.

## Verification

Before enabling release bundling, test on clean standard-user Windows 10 and
Windows 11 machines with:

* no OBS or FFmpeg installation;
* a conflicting OBS installation and hostile `PATH` entries;
* no NVIDIA GPU and an NVIDIA driver below the NVENC requirement;
* NVIDIA hardware with a current driver;
* Unicode and space-containing installation paths;
* online and offline WebView2 conditions;
* upgrade, uninstall, reinstall and low-disk-space scenarios.

The uninstaller removes application/runtime files but never deletes clips or
user settings without an explicit future user-data flow.

## Licensing

`LICENSE`, `THIRD_PARTY_NOTICES.txt`, `packaging/windows/licenses.lock.json`,
the runtime manifest and the SBOM must ship with every release. The exact
FFmpeg configuration, x264 build, OBS source and all selected redistribution
terms must be audited before changing the lock to `approved`.
