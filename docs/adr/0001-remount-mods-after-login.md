# 0001: Remount mods after the login unmount instead of refusing it
**Status:** Accepted · **Date:** 2026-10-14

## Context
Since the September 2026 update the game unmounts every pak in `Paks/~mods` during login. Release 2.0.0 added an
unmount guard that refuses this for cosmetic mods. In October 2026 players reported bans attributed to unmount
blockers, and other authors withdrew theirs. Static analysis (`docs/re-findings.md`) shows that a refused unmount
returns failure to its caller, and that the game can list mounted paks and their files. Project Galacta, which lets
the unmount happen and then mounts the mods again, has no ban reports so far.

## Options
- **Keep refusing the unmount.** Already built, but it is the behaviour linked to the ban reports, and the failure
  return is a signal no clean client produces.
- **Leave the unmount alone.** No exposure after login, but mods are gone for the session.
- **Tell users to install Galacta.** No new code, but it depends on a separate mod and the user wanted it built in.
- **Remount after login is detected, as Galacta does.** Closest to Galacta's timing, but the exe has no clean native
  "login finished" signal (the login panel is Python, and the engine's map-load log strings are stripped).
- **Remount inside the unmount hook, right after the game's unmount returns.** Uses the engine's own `Mount` with the
  exact arguments recorded at startup, so load order is preserved and nothing depends on timing. Mods are absent only
  for that call, and the caller sees a successful unmount.

## Decision
Remount inside the unmount hook, on by default (`ModsAfterLogin=remount`), with `keep` and `off` available. The user
chose this: it adopts Galacta's mechanism without its dependency and avoids the failure return.

## Consequences
- The plugin hooks `FPakPlatformFile::Mount` from startup to record each `~mods` mount; a second game function to
  locate after updates (`--probe` reports it).
- A pak unmounted again within 10 seconds of its remount stays unmounted, so a game script that insists wins instead
  of looping; a later login (after a disconnect, say) is remounted again.
- `Mount` runs inside the game's unmount call, after the engine's own unmount has returned and released its lock.
  Whatever locks the caller holds (a Python script, a delegate) are still held. Static analysis can't rule out a
  deadlock there; the first in-game run is the test. If one shows up, the fallback is to defer the remount.
- Mods remain visible to the game's pak-listing functions after login, as with Galacta or the guard. Revisit if
  evidence shows how the game treats remounted mods.
