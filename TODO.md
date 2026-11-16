# TODO

## Re-anchor the Performance DLC binder locator

`LocateSigningKeysBinder` currently finds the binder by a ~60-byte prologue pattern plus a hard-coded
`+0x2d` offset to its count operand. A recompile can break either, and the Experimental Performance DLC
build is experimental, so rebuilds are expected.

The locator already recovers the delegate-size global (COUNT) independently: from the `Missing signature`
marker in `.rdata` and the inlined `cmp dword [rip+COUNT], 0` sites around it. The durable form is to
identify the binder as the unique function that stores 2 to COUNT — scan for
`C7 05 <rel32 to COUNT> 02 00 00 00` (confirmed present at RVA `0x10e4fff` on the 2026-11-14 build,
SHA-256 `d56583e8…40eb7`) and take its enclosing function start via the existing padding walk, tolerating
a store-via-register form as a fallback. That needs no binder prologue bytes, no `+0x2d` and no `0x148`.

Deferred from the 2026-11-16 review because it changes which function is located, and the pattern-based
locator is the one confirmed working in game; the replacement needs its own in-game check.
