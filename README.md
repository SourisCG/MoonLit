# MoonLit

Open-source, lightweight, local-first game clip recorder for Linux and Windows.
A Medal.tv-style alternative with zero cloud: replay buffer, lightweight editor,
and sharing through your own storage (Google Drive) and webhooks.

See [`SPEC.md`](./SPEC.md) and [`docs/`](./docs) for the full technical specification.

## Develop

```bash
pnpm install
pnpm tauri dev
```

## License

Copyright (C) 2026 SourisCG

This program is free software: you can redistribute it and/or modify
it under the terms of the **GNU General Public License version 3 only**,
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
[LICENSE](./LICENSE) file for details.

MoonLit is licensed `GPL-3.0-only` because its Linux capture engine,
[gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/)
(which is `GPL-3.0-only`), is integrated as a sidecar/subprocess.
