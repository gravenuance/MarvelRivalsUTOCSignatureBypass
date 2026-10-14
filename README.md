# MarvelRivalsUTOCSignatureBypass

An ASI plugin for Marvel Rivals that lets client-side cosmetic mods load and keeps them loaded after login.

- **Signature bypass.** The game accepts mod containers (`.pak`/`.utoc`/`.ucas`) that are not signed.
- **Mods after login.** Since the September 2026 update the game unmounts everything in `Paks/~mods` during login. The plugin lets that happen and then mounts each cosmetic mod again, the same way the engine first mounted it. This is Project Galacta's approach, built into the plugin, so you don't need Galacta.
- **Intro skip.** The NetEase, Marvel and Unreal logo videos at startup are skipped.

Fork of [DeathChaos25/MarvelRivalsUTOCSignatureBypass](https://github.com/DeathChaos25/MarvelRivalsUTOCSignatureBypass) ([Nexus page](https://www.nexusmods.com/marvelrivals/mods/2940)).

## Install

Needs an ASI loader (for example `dsound.dll` in `MarvelGame/Marvel/Binaries/Win64`).

1. Put `MarvelRivalsUTOCSignatureBypass.asi` in `MarvelGame/Marvel/Binaries/Win64/plugins`.
2. Remove `MarvelRivalsUnmountBlocker.asi` if you have it. It hooks the same function, and this plugin stands down if the other got there first.
3. Put mods in `MarvelGame/Marvel/Content/Paks/~mods` (subfolders are fine).

Settings go in an optional `MarvelRivalsUTOCSignatureBypass.ini` next to the ASI:

```ini
[Settings]
Version=1
; 0 keeps the intro videos (default 1: skip them)
SkipIntroVideos=1
; remount (default), keep, or off
ModsAfterLogin=remount
```

A missing file or key uses the default.

## Mods after login

During login the game unmounts every pak in `Paks/~mods`. `ModsAfterLogin` decides what happens next:

- **`remount`** (default): the unmount goes ahead, and right after it the plugin mounts the mod again with the same arguments the engine used at startup, so load order is unchanged. If the game unmounts a mod again within 10 seconds of remounting it, the plugin lets that unmount stand rather than fight over it, and the log says so. A later login, such as after a disconnect, is remounted as usual.
- **`keep`**: the plugin refuses the unmount, so mods never leave. This is what `MarvelRivalsUnmountBlocker.asi` did. Players have reported bans for it, and a refused unmount reports failure back to the game.
- **`off`**: the game's unmount goes ahead and mods are gone until the next start.

In every mode the game can list which paks are mounted and what's inside them ([findings](docs/re-findings.md)). Nothing confirms that it acts on that list, but remounted or kept mods are visible to it. Use mods online at your own risk.

### Which mods are remounted or kept

Every pak under `Paks/~mods`, **except** a container whose `.utoc` lists `AbilitySystem` or `CameraShake` assets. Those can change gameplay, so they stay unmounted. This is the same rule Project Galacta uses.

A pak whose contents can't be checked counts as cosmetic: no `.utoc` next to it, a `.utoc` that can't be read or is over 64 MB, or one whose asset names are encrypted.

## Log

Everything the plugin does goes to `MarvelRivalsUTOCSignatureBypass.log` next to the ASI. The previous run's log is kept as `.log.1`. A healthy start looks like:

```
INFO  Signature bypass installed at .text+0xf26700
INFO  Mod remount installed (unmount at .text+0x1a3b7a0, mount at .text+0x1a2e720)
INFO  Intro skip installed
INFO  Startup finished in … ms (signature bypass on, mods after login remount, intro skip on)
INFO  Skipped intro video: …\MarvelGame\Marvel\Content\Marvel\MoviesBink\Movies\MarvelLogo\COMMON\MarvelLogoVideo_Common.bk2
INFO  Remounted (cosmetic mod): ../../../Marvel/Content/Paks/~mods/zSkin_9999999_P.pak
```

If a game update moves things, the log says which lookup failed and why, and that part is skipped rather than guessed. If a lookup for mods after login fails, nothing is hooked and mods unload at login as they would without the plugin.

## How it works

The game hooks are located at runtime, not from hard-coded addresses.

- **Signing keys:** upstream's byte pattern at the call site of the function that returns the pak signing keys. The hook returns an empty key list. The pattern must match exactly once, and the function it calls must look like the engine's getter (a guarded static it returns the address of), or nothing is hooked.
- **Unmount** (`remount` and `keep`): NetEase's pak-unmount wrapper logs `Unmounting pak file: %s` and then jumps into the engine's `FPakPlatformFile::Unmount`. The plugin finds that string, the code that references it, and the jump, and checks the target's first bytes before hooking it.
- **Mount** (`remount` only): `FPakPlatformFile::Mount` is the only code that loads the string `_P.pak`. The plugin finds that reference, walks back to the start of the function (the nearest double int3 padding), and checks its first bytes. From startup on, the hook records how the engine mounts each pak in `~mods`, then replays that call after the login unmount.
- **Intro skip:** the game picks its logo videos from a data table, so no ini setting turns them off, and the game's integrity check may restore deleted files. Instead the plugin hooks the Windows file functions (`CreateFileW`, `CreateFile2`, `GetFileAttributesW`, `GetFileAttributesExW` in KernelBase) and reports anything under `MoviesBink/Movies/MarvelLogo/` as not found. Every other file, including the lobby videos, passes straight through.

Galacta remounts from a Blueprint once it sees that login has finished. The plugin remounts inside the unmount call instead: the engine tells it exactly which pak just left, and replaying the engine's own mount call keeps the original load order without guessing it.

## Build

Requires Visual Studio 2022 Build Tools (MSVC v143) and the Windows 10+ SDK. [Microsoft Detours](https://github.com/microsoft/Detours) is a git submodule pinned to a specific commit and built from source with the rest of the solution, so clone with submodules:

```
git clone --recurse-submodules https://github.com/gravenuance/MarvelRivalsUTOCSignatureBypass.git
msbuild MarvelRivalsUTOCSignatureBypass.sln /p:Configuration=Release /p:Platform=x64
build\Release\Tests.exe
```

In an existing clone, run `git submodule update --init` first.

The plugin is `build\Release\MarvelRivalsUTOCSignatureBypass.asi`. Release builds treat every warning as an error, and CI runs the same two commands.

Local rebuilds with the same MSVC version produce a byte-identical ASI. CI builds of the same code have not yet been shown to match each other; each run logs its MSVC version.

Releases from 2.1.1 on ship `SHA256SUMS.txt` and a build provenance attestation. To check a download:

```
sha256sum -c SHA256SUMS.txt
gh attestation verify MarvelRivalsUTOCSignatureBypass.asi -R gravenuance/MarvelRivalsUTOCSignatureBypass
```

Two read-only checks against a real install:

- `Tests.exe --probe <path to Marvel-Win64-Shipping.exe>` runs both lookups against the game executable without running it, and prints where they land and how long they take. It also counts code that has its own inlined copy of the signing-keys getter; more than one copy (the function that registers the real keys) means some code could read the keys past the hook. Use it to check a new game build before launching.
- `Tests.exe --classify <path to Content\Paks>` runs the mod rules over everything in `~mods` and prints how each pak would be treated.

## License

LGPL-2.1, as upstream. See [LICENSE.txt](LICENSE.txt). Microsoft Detours (`external/Detours`) is MIT-licensed.
