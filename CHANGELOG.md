# Changelog

## [Unreleased]

### Changed
- Mods after login are remounted rather than kept: the plugin lets the game's login unmount go ahead, then mounts each cosmetic mod again with the arguments the engine first used. A mod unmounted again within 10 seconds of its remount stays unmounted, so the plugin never loops against the game. This is Project Galacta's approach built in. Players reported bans for keeping mods mounted through login, and a refused unmount reports failure to the game.
- `ModsAfterLogin=keep` in the ini restores the old behaviour, and `ModsAfterLogin=off` leaves the unmount alone.

### Added
- `Tests.exe --probe` also reports where `FPakPlatformFile::Mount` was found.

## [2.1.1] - 2026-10-01

### Added
- Releases ship `SHA256SUMS.txt` and a GitHub build provenance attestation for the ASI.
- `--probe` reports code that inlines the signing-keys getter, which would read the keys past the hook.

### Changed
- The signature bypass checks that the function it hooks is the signing-keys getter, and reports when another plugin has already hooked it.

## [2.1.0] - 2026-09-26

### Added
- Intro skip: the NetEase, Marvel and Unreal logo videos at startup are skipped. The files are left in place; the game just sees them as missing.
- Optional `MarvelRivalsUTOCSignatureBypass.ini` next to the ASI; `SkipIntroVideos=0` keeps the intro videos.

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
