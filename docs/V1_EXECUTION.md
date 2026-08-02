# MoonLit V1 Execution State

The coordinator and project agents use this file to prevent parallel work from
being mistaken for completed product behavior. Only `moonlit-coordinator` may
change this file.

## Current Baseline

- Source baseline: `9486f6ccc8f6ee49becec4375456c99d4ea83541` (`Alpha V1`).
- Current worktree: dirty from the project-scoped OpenCode team setup,
  coordinator-only Rust formatting fix, and Wave 0 documentation corrections.
- Last relevant report: `test-results/libobs-sidecar-scaffold-2026-07-29.md`.
- That report predates the current baseline and is not release evidence.
- Production libobs bridge: unavailable stub.
- Real WGC/WASAPI/MP4/MKV production pipeline: not verified.
- Release status: blocked.

## Wave State

| Wave | Owner group | Dependency | State | Exit condition |
|---|---|---|---|---|
| 0. Truth and baseline | Assurance, verifier, release | None | blocked | Current-SHA tests and truthful scope report; clean-tree evidence still missing |
| 1. Contracts and host integrity | Runtime, host, frontend | Wave 0 | pending | Safe host state and explicit capability tuples |
| 2. Native runtime | Native core, capture, assurance | Wave 1 | pending | Real libobs init, isolated runtime, WGC source |
| 3. First real clip | Native core, runtime, verifier | Wave 2 | pending | Decodable x264 H.264 MP4 replay |
| 4. Codec matrix | Codecs, capture, runtime | Wave 3 | pending | H.264 and HEVC tuple evidence |
| 5. Audio and proxy | Audio, host, frontend | Wave 4 | pending | WASAPI, A/V sync, proxy evidence |
| 6. Product integration | Host, frontend, reviewer | Wave 5 | pending | Host-owned actions and E2E flows |
| 7. Release closure | Release, assurance, verifier | Wave 6 | blocked | Approved closure, licenses, SBOM, installers, signatures |
| 8. External qualification | Hardware profiles, verifier | Wave 7 | blocked | Win10/Win11, GPU matrix, cycles, soak |
| 9. Promotion review | Assurance, verifier, reviewer | Wave 8 | blocked | All requirements verified on same artifact hashes |

## Operating Rules

- At most three active subagents and two heavy build jobs.
- No dependent wave starts while its gate is red or blocked.
- Shared contracts are changed only by the coordinator.
- Specialists return the handoff format in `.opencode/MOONLIT_AGENT_PROTOCOL.md`.
- A worker report is not a gate result until an independent verifier checks it.
- Do not use `completed` as a state.
