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

## 2026-11-13 update

Binary: `Marvel-Win64-Shipping.exe`, 307,753,472 bytes, dated 2026-11-13 (Steam),
SHA-256 `13a564a16588f65a7fd005f31bc08eb538555f3985dfa5801a313a8d9029d307`.

`Tests.exe --probe` (built from `a2719f8`) finds every hook with no code change: signing keys delegate
`.text+0xf69210` (one inlined getter copy, `.text+0xf775a0`), `Unmount` `.text+0x1a7e2e0`, `Mount` `.text+0x1a722b0`.
`MoviesBink/Movies/MarvelLogo/` still exists. Not yet checked in game.

## 2026-11-14 update: the game is now built with clang/LLVM

Binary: `Marvel-Win64-Shipping.exe`, 371,065,344 bytes, dated 2026-11-14 (Steam),
SHA-256 `d56583e835eea0ed9015212a1448b0b36c49dd7475d056294662ec4f08b40eb7`, image base `0x140000000`.
Analysed 2026-11-16 by PE parsing and byte scans over a copy. All three locators broke on this build.

The cause is a compiler change, not new packing or protection: the executable is compiled by clang/LLVM with
PGO/LTO and AVX, where every earlier build was MSVC. The prologues, the argument register the `_P.pak` marker is
loaded into and the guarded-static getter shape all follow from that. `.text` is still plain code.

**Gotcha: the section literally named `.pdata` is a decoy.** It is at RVA `0x14c76000`, but
`DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION]` points at RVA `0x15cd8700` (size `0x4e7374`), inside the
executable section `.a2!`. The live `RUNTIME_FUNCTION` table is the one the data directory names; function bounds
below come from it. `--probe` reads the directory, not the section name, so it was unaffected.

| Claim | Address (RVA) | Evidence | Status |
|---|---|---|---|
| Wide `Unmounting pak file: %s \n` | `0x1413deb0` | One copy, in `.rdata` | confirmed |
| NetEase's wrapper still loads it into rcx and tail-jumps to `Unmount` | `lea rcx` at `0x1f3f7a0`, `jmp` at `0x1f3f7c2` | The only reference to the string; `E9 C9 5D 00 00` resolves to `0x1f45590` | confirmed |
| `FPakPlatformFile::Unmount` | `0x1f45590`..`0x1f46b0f` | Exception-table entry for `0x1f45590`. Opens `41 57 41 56 41 55 41 54 56 57 55 53` (push r15, r14, r13, r12, rsi, rdi, rbp, rbx), then `sub rsp, 148h` | confirmed |
| Wide `_P.pak` | `0x13c7d2ee` | One copy, in `.rdata` | confirmed |
| Mount loads `_P.pak` into **r8**, not rdx | `lea r8` at `0x1f42918` | `4C 8D 05 CF A9 D3 11`, the only reference to the string in any register scanned (rcx, rdx, r8, r9, rax) | confirmed |
| `FPakPlatformFile::Mount` | `0x1f40f90`..`0x1f45586` | Exception-table entry for `0x1f42918`. Opens with the same twelve pushes, then `sub rsp, 5e8h`. 16-byte aligned, preceded by `CC CC`, and no `CC CC` pair between it and the marker, so the int3 walk-back reaches it and agrees with the table | confirmed |
| The marker sits `0x1988` bytes into Mount | — | `0x1f42918 - 0x1f40f90`. The old `0x1000` walk-back window could not reach the start; it is now `0x2800` | confirmed |
| The signing-keys call site is gone | — | `E8 ?? ?? ?? ?? 48 8B F8 39 70 ?? 0F 84 ?? ?? ?? ??` matches nowhere in `.text` (clang allocates registers differently). `--probe` reports `marker not found` | confirmed |
| The guarded-static getter has a new shape | — | `sub rsp,28h; mov eax,[rip+guard]; mov ecx,[rip+tls_index]; mov rdx,gs:[58h]; mov rcx,[rdx+rcx*8]; cmp eax,[rcx+148h]; jg init; lea rax,[rip+static]; add rsp,28h; ret`. Hundreds of functions share it, so the shape alone cannot identify the signing-keys one | confirmed |
| Which function returns the signing keys | — | Not identified. The locator is left failing with its existing clear log line rather than guessed at | open |

Two details of the new getter shape that the signature bypass would also have to handle, once the call site is
solved: it reads the TLS base into **rdx** (`65 48 8B 14 25 58 00 00 00`), not rax, so the plugin's
`ThreadLocalBaseRead` constant does not match it; and its fast path compares against a field of the TLS block
(`cmp eax, [rcx+148h]`), so there is no RIP-relative `cmp` on the guard to read the guard's address from.

Locator changes for this build: both the MSVC and the clang prologue are accepted for each function, the
`_P.pak` reference is searched for in rdx and r8, and the Mount walk-back window is `0x2800`.
`Tests.exe --probe` on this build: `FPakPlatformFile::Unmount .text+0x1f44590` (= RVA `0x1f45590`),
`FPakPlatformFile::Mount .text+0x1f3ff90` (= RVA `0x1f40f90`), signing keys `not found: marker not found`
(the signature bypass for this build came later, in the sections below).

## Why the signature bypass cannot be fixed by adjusting its pattern (2026-11-16)

The two clients are the base game and the "Experimental In-Game Performance" DLC (Steam appid 5554080,
depot 5554081). Turning the DLC off in Steam restores a `Marvel-Win64-Shipping.exe` that is **bit-identical**
to the 2026-11-13 build recorded above (307,753,472 bytes, SHA-256 `13a564a1…9d307`), so both binaries can be
held side by side. `--probe` on the restored one finds every hook at the documented addresses, which also
confirms the locator changes above cause no regression on MSVC.

Toolchain discriminator, if one is ever needed: the DLC build has **no Rich header** and reports linker
version `14.0` (lld-link); every MSVC binary checked has one — base `Marvel-Win64-Shipping.exe` 14.40,
`Marvel.exe` 14.51, `CrashReportClient.exe` 14.40, `Marvel-Win64-Shippingarm64ec.exe` 14.51 (that one is the
Windows-on-ARM build, MSVC-built, and shipped with the base update, not the DLC). Obfuscated section names
(`.@p~`, `.sSd`, `.8P2`) appear on the MSVC builds too, so they are a NetEase packer trait, not a clang artifact.

**Method.** Code changes wholesale between compilers but string literals do not. The signing-keys getter is
called from UE's IoStore TOC reader, which logs `Missing signature` and `TOC header magic mismatch while
reading '`. Walking from those strings to the referencing function and then to the small guarded-static
functions it calls finds **exactly one** candidate on the MSVC build — `0xf6a210`, which is the getter the
plugin already locates (`.text+0xf69210`). Same walk on the clang build: the anchors land in the equivalent
function `0x105f230..0x10608eb`, but it calls **no** getter-shaped function at all.

| Claim | Address (RVA) | Evidence | Status |
|---|---|---|---|
| MSVC: signing-keys getter returns a static | getter `0xf6a210`, static `0xfd422b0` | `lea rax,[rip+…]` tail; one candidate from the string walk | confirmed |
| MSVC: the bypass works by making a count at `static+8` zero | call site `0xf28991` | `call 0xf6a210; cmp dword [rax+8], esi; je skip` with `esi`=0 — zero means no keys, so the check is skipped; otherwise `mov rcx,[rax]; call qword [rax+30h]` verifies | confirmed |
| clang: the getter is **inlined**, so no function remains to hook | — | 5 inlined TLS-guarded static reads inside `0x105f230..0x10608eb`; the sequence at `0x105fec9` is the same logic with the call replaced by `cmp dword [rip+…],0; je; mov rcx,[rip+…]; call qword [rax+30h]` | confirmed |
| clang: the same state lives in two adjacent globals | object `0x1475f8b0`, count `0x1475f8b8` | Resolved from `0x105feee` and `0x105ff0d`; 8 apart, matching the MSVC `+0`/`+8` layout | confirmed |
| clang: the count is read from many inlined sites | 20 references | `cmp dword [rip+…],0` at `0x105feee`, `0x10601bb`, `0x1060224`, `0x1060262` (TOC reader), `0x5109f`/`0x5109df` (in `0x50f9b0..0x5110c9`), plus `mov dword [rip+…],0` writes at `0x1060736`, `0x1060783`, `0x510ff4` | confirmed |
| The count may be a state enum, not only a count | `0x10e4f90..0x10e50a6` | That function reads it, makes an indirect call, re-reads it and does `cmp eax, 2` — so `2` is a meaningful state and something re-initialises it | hypothesis |

**Consequence.** Patching a branch is whack-a-mole: there are many inlined check sites and they all read one
global, so the equivalent of upstream's "return an empty key list" is to make that shared global read as zero.
When to write it, and whether the initialiser at `0x10e4f90` puts it back, are open. Mods 14652 and 14628 on
Nexus already support this build; our fork and upstream are both **LGPL-2.1**, and 14652 is described as a
modified build of DeathChaos's bypass, so its source should be obtainable. Approach not yet decided.

**Tooling pitfall.** RIP-relative disp32 is relative to the *end* of the instruction, so `cmp dword [rip+d], 0`
(`83 3D <disp32> 00`) ends one byte past the disp32. A scanner that assumes the instruction ends right after
the disp32 silently misses every RIP-relative instruction with a trailing immediate — it reported 2 references
to the count global instead of 20. `lea`/`mov reg,[rip+d]` are unaffected, so string-reference counts taken
that way still stand.

## The hook target on the clang build (2026-11-16)

Not every guarded-static getter was inlined away. One survives as a real function and is the analogue of the
MSVC getter, so the plugin's existing "locate, verify, hook" shape still applies — no branch patching needed.

| Claim | Address (RVA) | Evidence | Status |
|---|---|---|---|
| clang: the signing-keys accessor survives as a function | `0x10e4f90`..`0x10e50a6` | `push rsi; sub rsp,20h; mov rsi,rcx;` guarded-static preamble; `mov eax,[rip+COUNT]; test eax,eax; je +1Eh; mov rcx,[rip+OBJECT]` | confirmed |
| It is the **signing-keys** one, not a sibling | — | Both reads of the count global `0x1475f8b8` (`0x10e4fbd`, `0x10e4fda`) lie inside it; that global is the one the TOC reader's signature check tests | confirmed |
| A sibling function matches the same prologue | `0x10e50b0` | Same byte pattern, adjacent; references a different global | confirmed |
| The MSVC and clang patterns are mutually exclusive | — | Upstream's `E8 ?? ?? ?? ?? 48 8B F8 39 70 ?? 0F 84 ?? ?? ?? ??`: 1 match on the base build (`0xf28ade`), 0 on the DLC build. The clang accessor prologue: 2 matches on the DLC build, 0 on the base build | confirmed |

So a single plugin build can keep using "try each form, install whichever verifies", as it now does for the
prologues. Because the accessor pattern matches twice, a locator must disambiguate rather than take the first
hit: resolve the `mov eax,[rip+disp]` count operand and require it to equal the global read by the inlined
signature check in the TOC reader (reachable from the `Missing signature` anchor). That is stricter than
matching on the prologue alone and keeps the existing `Ambiguous` safety property meaningful.

Still open: what the replacement must do to present "no keys" — the accessor takes an object in rcx and the
inlined sites store a result byte, so its contract needs pinning down before anything is hooked.

Cross-check against Nexus mod 14652 (downloaded for analysis; Detours-based, 45 KB, no logging, **no hardcoded
addresses**). It carries three masked byte patterns: upstream's MSVC call site (labelled
`GetPakSigningKeysDelegate_Sig`), the clang accessor prologue above, and one inlined check site
(`83 3D ?? ?? ?? ?? 00; 74 14; 48 8B 0D …; test rcx,rcx; je; mov rax,[rcx]; call qword [rax+30h]; jmp;
xor eax,eax; mov [rsi+38h],al`). It confirms the accessor is the intended hook target and reaches the same two
builds the same way. Its patterns and addresses are facts about the game binary, not copyrightable expression;
any implementation here is to be written from this analysis, with credit to that author in the README.

## What the binder does, and how it is told from its sibling (2026-11-16)

Binary: the DLC build, SHA-256 `d56583e835eea0ed9015212a1448b0b36c49dd7475d056294662ec4f08b40eb7`. Disassembled
`0x10e4f90..0x10e50a6` in full, and measured the scans below over the whole `.text` of both kept builds.

**The binder's contract.** It is the lazy guarded-static accessor for the signing-keys delegate, with the bind
folded into it. After the TLS guard it reads `COUNT` (`0x1475f8b8`); if that is non-zero and `OBJECT`
(`0x1475f8b0`) is set it calls the delegate's slot 0 with `edx = 0` and re-reads `COUNT`; unless `COUNT` is
already 2 it calls `0x4e3041e0(&OBJECT, 0, 2, 0x10)` and does `mov dword [COUNT], 2`. It then writes a vtable
into the allocation, takes a refcount id from a `lock xadd` on a global, writes it to `+0x10`, writes a second
vtable, stores its rcx argument at `+0x18`, and **returns the allocation in rax**. `OBJECT`/`COUNT` are
`FDelegateBase`'s `{ void* allocation; int32 delegateSize; }`, the same `+0`/`+8` pair the MSVC callers read
through the getter's return value.

So the replacement has to call the original: callers use the returned pointer and the writes it makes.
Zeroing `COUNT` afterwards is the clang equivalent of upstream's empty key list — the ~20 inlined
`cmp dword [rip+COUNT], 0` sites then all take the "no signing keys" path, and a later re-bind is re-zeroed by
the same hook. `COUNT` is in `.data` (RW), so the write needs no page protection change.

| Claim | Address (RVA) | Evidence | Status |
|---|---|---|---|
| The binder returns the delegate storage and must be called | `0x10e4f90`..`0x10e50a6` | `mov rax,[OBJECT]`, two vtable stores, `lock xadd` refcount, `mov [rax+18h],rsi`, `ret` | confirmed |
| Binding marks the delegate with `COUNT = 2` | write at `0x10e4fff` | `mov dword [rip+0x1367a8af], 2`, resolving to `0x1475f8b8` | confirmed |
| The 62-byte prologue pattern matches twice, and only on this build | `0x10e4f90`, `0x10e50b0` | 2 matches here, 0 on the base build; upstream's MSVC pattern is the other way round | confirmed |
| The two candidates mark different globals | `0x1475f8b8` and `0x1475f8a0` | `mov eax,[rip+disp32]` at function + `0x2d`, six bytes long | confirmed |
| Wide `Missing signature` is referenced once, by an **AVX load**, not a `lea` | string `0x13d251d0`, load `0x106010e` | `C5 FE 6F 05 <disp32>` (`vmovdqu ymm0,[rip+disp32]`), eight bytes long; the only reference to the string anywhere in the image | confirmed |
| Around that load, the signature check reads one global and no other | sites `0x105feee`, `0x10601bb`, `0x1060224`, `0x1060262` | Every `83 3D ?? ?? ?? ?? 00` within `0x1000` bytes either side resolves to `0x1475f8b8`; 4 sites, 1 distinct target | confirmed |

**The disambiguation rule the locator uses.** Match the prologue pattern; resolve each candidate's `COUNT` from
the `mov eax,[rip+disp32]` at candidate + `0x2d` (length 6); find the wide marker and its one AVX load; collect
the targets of every `cmp dword [rip+disp32], 0` within `0x1000` bytes either side of that load; keep the
candidates whose `COUNT` is among them. Exactly one must survive, otherwise `LocateError::Ambiguous`. The window
stands in for the enclosing function: the TOC reader is `0x105f230..0x10608eb`, about `0x16bb` bytes, and the
measurement above shows the window reaches its four check sites without picking up a neighbour's.

**The pitfall again, because both halves of the rule step on it.** `cmp dword [rip+d], 0` carries a trailing
`imm8`, so its displacement is measured seven bytes on, and the AVX load's is measured eight bytes on. Reading
either as if the instruction ended at the displacement lands one byte short — on this build, one byte short of
`COUNT` is `0x1475f8b7`, which is inside the pointer field and would quietly match nothing.

`Tests.exe --probe` on the DLC build now reports `signing keys binder .text+0x10e3f90` (= RVA `0x10e4f90`) and
`delegate size at rva 0x1475f8b8`, and exits 0; on the base build it still reports the getter at `.text+0xf69210`
and no binder.

**Confirmed in game on 2026-11-16**, on the DLC build (`Marvel-Win64-Shipping.exe` SHA-256 `d56583e8…40eb7`,
the same binary this analysis was derived from, re-verified after reinstalling the DLC): the plugin installs the
bypass at the binder, cosmetic mods load, and they survive the login unmount. So zeroing the delegate size after
the binder runs is sufficient to make all of the inlined checks read as "no keys" — the hypothesis this design
rested on holds.
