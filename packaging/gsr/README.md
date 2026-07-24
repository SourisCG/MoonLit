# GSR Release Input

The first local GSR installation was useful for CLI investigation, but it is
not a release input. The release pipeline must pin an upstream source revision
and checksum before producing an RPM.

Required release inputs:

- audited upstream source archive or git revision;
- `sourceRevision` and `sourceSha256` in `gsr.lock.json`;
- x86_64 build output for `gpu-screen-recorder`;
- x86_64 build output for `gsr-kms-server`;
- the matching GSR license and source-code information;
- a generated `components.json` with binary hash and build metadata.

The build must fail when the revision or source checksum is missing. It must
not silently use a system GSR, a random download or the developer's
`~/.local/bin` installation.
