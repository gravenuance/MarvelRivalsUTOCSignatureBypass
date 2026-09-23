# MarvelRivalsUTOCSignatureBypass

An ASI plugin for Marvel Rivals that lets client-side cosmetic mods load and stay loaded.

- **Signature bypass.** The game accepts mod containers (`.pak`/`.utoc`/`.ucas`) that are not signed.
- **Unmount guard.** Since the September 2026 update the game unmounts everything in `Paks/~mods` during login. The plugin refuses that unmount for cosmetic mods, so they stay loaded. This replaces both `MarvelRivalsUnmountBlocker.asi` and Project Galacta's remount step.

Fork of [DeathChaos25/MarvelRivalsUTOCSignatureBypass](https://github.com/DeathChaos25/MarvelRivalsUTOCSignatureBypass) ([Nexus page](https://www.nexusmods.com/marvelrivals/mods/2940)).

## Install

Needs an ASI loader (for example `dsound.dll` in `MarvelGame/Marvel/Binaries/Win64`).

1. Put `MarvelRivalsUTOCSignatureBypass.asi` in `MarvelGame/Marvel/Binaries/Win64/plugins`.
2. Remove `MarvelRivalsUnmountBlocker.asi` if you have it. Both hook the same function, and this plugin stands down if the other got there first.
3. Put mods in `MarvelGame/Marvel/Content/Paks/~mods` (subfolders are fine).

Project Galacta's menu still works alongside this plugin, but you don't need Galacta for mods to load.

## Which mods stay loaded

Every pak under `Paks/~mods` is kept, **except** a container whose `.utoc` lists `AbilitySystem` or `CameraShake` assets. Those can change gameplay, so the game is allowed to unmount them. This is the same rule Project Galacta uses. A pak with no `.utoc` next to it is kept.

## Log

Everything the plugin does goes to `MarvelRivalsUTOCSignatureBypass.log` next to the ASI. The previous run's log is kept as `.log.1`. A healthy start looks like:

```
INFO  Signature bypass installed at .text+0xf0a870
INFO  Unmount guard installed at .text+0x1a17690
INFO  Startup finished in … ms (signature bypass on, unmount guard on)
INFO  Kept mounted (cosmetic mod): ../../../Marvel/Content/Paks/~mods/zSkin_9999999_P.pak
```

If a game update moves things, the log says which lookup failed and why, and that part is skipped rather than guessed.

## How it works

Both hooks are located at runtime, not from hard-coded addresses.

- **Signing keys:** upstream's byte pattern at the call site of the function that returns the pak signing keys. The hook returns an empty key list. The pattern must match exactly once, or nothing is hooked.
- **Unmount:** NetEase's pak-unmount wrapper logs `Unmounting pak file: %s` and then jumps into the engine's `FPakPlatformFile::Unmount`. The plugin finds that string, the code that references it, and the jump, and checks the target's first bytes before hooking it.

Why hook `Unmount` rather than remount afterwards like Galacta? Every unmount, whatever triggers it, goes through that one function, and the hook sees the engine's own result. Refusing it means mods are never missing even for a moment, and nothing depends on timing or on the login screen's widget names. The engine's mount function has no "already mounted" check, so blind remounting risks mounting a pak twice.

## Build

Requires Visual Studio 2022 Build Tools (MSVC v143) and the Windows 10+ SDK.

```
msbuild MarvelRivalsUTOCSignatureBypass.sln /p:Configuration=Release /p:Platform=x64
build\Release\Tests.exe
```

The plugin is `build\Release\MarvelRivalsUTOCSignatureBypass.asi`. Release builds treat every warning as an error, and CI runs the same two commands.

Local rebuilds with the same MSVC version produce a byte-identical ASI. CI builds of the same code have not yet been shown to match each other; each run logs its MSVC version.

Two read-only checks against a real install:

- `Tests.exe --probe <path to Marvel-Win64-Shipping.exe>` runs both lookups against the game executable without running it, and prints where they land and how long they take. Use it to check a new game build before launching.
- `Tests.exe --classify <path to Content\Paks>` runs the mod rules over everything in `~mods` and prints how each pak would be treated.

## License

LGPL-2.1, as upstream. See [LICENSE.txt](LICENSE.txt). The bundled Microsoft Detours library is MIT-licensed.
