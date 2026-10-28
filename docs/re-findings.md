# Reverse-engineering findings

Binary: `Marvel-Win64-Shipping.exe`, 302,710,272 bytes, dated 2026-10-09 (Steam),
SHA-256 `5b323de46f7c6e57bc4057381ff893fc623cbb1c20f1ef6218deda1ac63a3860`, image base `0x140000000`.
Addresses are RVAs. Analysed 2026-10-14 with Ghidra 12.1.4 headless (`D:/Tools/ghidra-targeted`) and byte scans.

`.text` is plain code. `.7&I` (5 MB) and `.Op)` (16 MB) are executable, high-entropy VMProtect sections, and
`.pdata` is scrambled, so function bounds come from Ghidra or the int3 padding.

## Unmount path

| Claim | Address | Evidence | Status |
|---|---|---|---|
| `FPakPlatformFile::Unmount` (the hook target) | `0x1a3c7a0` | Removes the pak from the mounted list at `this+0x10` (count `+0x18`, lock `+0x40`), then from the IoStore container set via `this+0x128` | confirmed |
| NetEase's wrapper logs `Unmounting pak file: %s` and tail-jumps to `Unmount` | `0x1a2a2d0` | String `0xa846630`, `jmp` at `0x1a2a324` | confirmed |
| The wrapper is bound as a delegate handler, not called directly | `0x1a2c059` | Only reference: stores `&0x1a2a2d0` into `+0x20` of a delegate object. Matches UE's `OnUnmountPak` binding in `FPakPlatformFile::Initialize` | hypothesis |
| Python can call `Unmount` directly as `UnmountPak(path)`, which returns `True`/`False` | `0x7dfc280`, PyMethodDef `0xf82cef8` | Returns `Py_True` (`0x14f926128`) or `Py_False` (`0x14f926108`) from the call result | confirmed |
| Other `Unmount` call sites | `0x7e1ba70` (static helper, inlined copy), ChunkDownloader `UnmountPak` (string `0xcb89dd0`) | — | confirmed |
| What triggers the login-time unmount of `~mods` | — | Not visible natively; likely a Python script calling `UnmountPak` or firing `OnUnmountPak` | open |

## What the game can see while a mod stays mounted

| Claim | Address | Evidence | Status |
|---|---|---|---|
| `GetMountedPakNames()` (Python) returns every mounted pak path | `0x7dfc010` | Walks the same `this+0x10` array that `Unmount` removes from | confirmed |
| `UAssetFunctionLibrary::GetMountedPakFileList(s)(name)` returns the files inside a mounted pak whose name matches | thunks `0x59a79d0`/`0x59a7140`, native `0x6addef0`/`0x6ade1a0` | Matches by name (`0x140f5a840`), then lists the pak's contents (`0x141a29150`) | confirmed |
| A refused unmount is visible to the caller | `0x7dfc280` | `UnmountPak` returns `False` for that path; a stock client never gets `False` for a pak that is mounted | confirmed |
| Python game logic (where any check would live) | — | Not in any container's directory index and not loose on disk. Game logs (`%LOCALAPPDATA%/Marvel/Saved/Logs`) are encrypted (`logversion000` header) | not analysed |
| Anti-cheat code in the VMProtect sections | `.7&I`, `.Op)` | Strings `AntiCheatInstanceTable`, `UMarvelCheatAnalyzer`, `UAntiCheatScreenShotBPLibrary` exist | not analysed |

## Unrelated local record

`FUN_141a3e450` (called from the startup mount code at `0x1a2f720`) writes `Saved/pak_invalid.txt`: paks in the
mount list that are missing from a second list at `this+0x28`. In the analysed session it held exactly the 14 mods
that have no `.utoc`. It is written at startup whether or not the plugin is installed. Whether it is uploaded is
unknown.

## Project Galacta 1.4.1

Source: `Project Galacta Mod 12806 1.4.1` zip, SHA-256 `c58eddf4d41aec5bb73129f18505f4e82f2435b123ea1d28cf6d352d8a02e076`.
A single IoStore mod (`!ProjectGalacta_9999999_P`); no native code. Read from its packages' name tables and imports.

| Claim | Evidence | Status |
|---|---|---|
| `GAL_ModLoader` polls for login on a timer, then remounts | Names `PollLogin`, `LoginPollHandle`, `bSawLoginPanel`, `PyLoginPanelV3`, `GetCurrentLevelName`, `K2_SetTimerDelegate` | confirmed (names), order of events hypothesis |
| It mounts only paks missing from the game's mount list | `CallFunc_GetMountedPakNames_PakFilenames`, `MountedPaths`, log text `Mounted before:` next to `CallFunc_MountPak_ReturnValue` | hypothesis (graph not decoded) |
| It skips gameplay paks and sorts mount order | `IsPakAllowed`, `SortPaks`, `MountOrder` | confirmed (names) |
| It can unmount mods on demand | `ToggleMods`, `CallFunc_UnmountPak_ReturnValue`, `[ModLoader] mods UNMOUNTED:` | confirmed (names) |

## Mount path (used by the remount)

| Claim | Address | Evidence | Status |
|---|---|---|---|
| `FPakPlatformFile::Mount(this, path, order, mountPoint, bLoadIndex)` | `0x1a2f720` | Builds the `FPakFile`, parses `_P.pak` for patch order, mounts the `.utoc` through `this+0x128`, calls the `pak_invalid.txt` writer | confirmed |
| It takes five arguments, and the order passed in is the base order (Mount adds the `_P.pak` bonus itself) | callers `0x7e1649f` (NetEase `MountPak` helper `0x7e16430`) and `0x1a2a0bb` (`HandleMountPakDelegate`, `0x1a2a050`) | Both set `rcx`, `rdx`, `r8d`, `r9`, `[rsp+20h]=1` and nothing past it | confirmed |
| Only reference to wide `_P.pak` | `lea rdx` at `0x1a2fa04`, string `0xa846198` | 0x2e4 bytes into Mount. No `CC CC` between the start and it, two single `CC` bytes inside instructions | confirmed |
| Python `MountPak` / `MountPakWithMountPoint` | `0x7dfc640` / `0x7dfc3f0` | Both call the helper `0x7e16430`, which looks up the platform file and calls Mount | confirmed |
| `Tests.exe --probe` on this build | — | `FPakPlatformFile::Mount .text+0x1a2e720` (= RVA `0x1a2f720`) | confirmed |

## Mount's return value for a pak with no `.utoc`

Binary: `Marvel-Win64-Shipping.exe`, 308,987,392 bytes, dated 2026-10-23 (Steam),
SHA-256 `62e3cb096fd7cdaa88a8718e9c9682ee4d8b8ac7266b586b72b1865e13675657`. Analysed 2026-10-28, same tools.

| Claim | Address | Evidence | Status |
|---|---|---|---|
| `FPakPlatformFile::Mount` on this build | `0x1a70c90` | `Tests.exe --probe`: `.text+0x1a6fc90` | confirmed |
| Mount adds the pak to the mounted list at `this+0x10` before it looks for the `.utoc` | `0x1a70c90` | The array append and refcount bump come before the `this+0x128` IoStore branch | confirmed |
| With no `.utoc`, Mount returns false but leaves the pak mounted | `0x1a70c90` | `FileExists(path + ".utoc")` (vtable `+0x78`) fails, the result flag is cleared and `IoStore container "%s" not found` (`0x14ab46fd0`) is logged; nothing removes the pak from `this+0x10` | confirmed |
| A pak-only mod's content is used even so | — | Audio mods (pak-only, `~mods/audio`) played in 2.1.x, when the unmount was refused | confirmed (in game) |

2.2.0's Mount hook recorded only calls that returned true, so pak-only mods were never recorded and stayed
unmounted after login. The plugin log on 2026-10-25 and 2026-10-27 shows `Left unmounted (first mount not seen ...)`
for all 14 audio paks and `Remounted` for every mod that has a `.utoc`.
