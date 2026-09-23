# Changelog

## [Unreleased]

## [2.0.1] - 2026-09-24

### Changed
- Microsoft Detours is built from source, pinned to commit `adb07604aa` (August 2026) as a git submodule, instead of an unversioned prebuilt `detours.lib`. The last tagged release, 4.0.1, is from 2018; `main` carries the fixes since.

## [2.0.0] - 2026-09-24

### Added
- Unmount guard: cosmetic mods in `Paks/~mods` stay mounted when the game unmounts them at login (the September 2026 update). Replaces `MarvelRivalsUnmountBlocker.asi` and Project Galacta's remount step.
- Mods whose container lists `AbilitySystem` or `CameraShake` assets are not protected, matching Galacta's rule.
- A log file next to the ASI, keeping the previous run as `.log.1`.
- `Tests.exe --probe <exe>` checks both lookups against a game build without running it; `--classify <Paks>` shows how each installed mod would be treated.
- Deterministic local builds: rebuilding with the same MSVC version gives a byte-identical ASI.

### Changed
- Both hooks are found at runtime and refused if the match is ambiguous or the target looks wrong.
- Startup scan is faster: about 10 ms per lookup once pages are resident.
- x64-only build with static runtime, so no VC++ redistributable is needed.

### Removed
- Unused upstream code: TOML config, file-access logging stubs and the debug console.
