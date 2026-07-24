# Fedora GNOME Workstation

Role: complete development and functional testing environment.

- Distribution: Fedora (version collected by the app `run_doctor` command or
  `npm run tauri -- info`).
- Desktop: GNOME.
- Session: expected Wayland; verify at runtime.
- Performance class: lower-powered development machine.
- Exclusive responsibilities: none.

Use adaptive profiles. Prefer fake backend, 720p30 smoke tests and short
native tests when the machine is under load.
