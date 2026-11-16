#pragma once
#include <cstdint>
#include <optional>

#include "ByteSearch.h"

namespace bypass
{
    enum class LocateError
    {
        MarkerMissing,
        ReferenceMissing,
        PrologueMissing,
        PrologueMismatch,
        AlreadyHooked,
        Ambiguous,
        CountUnmatched,
        TargetOutsideCode,
        UnexpectedTarget,
    };

    struct Located
    {
        std::uintptr_t address = 0;
        LocateError error = LocateError::MarkerMissing;
        [[nodiscard]] bool Found() const noexcept { return address != 0; }
    };

    [[nodiscard]] const char* Describe(LocateError error) noexcept;

    // The static delegate a getter returns, and the guard of its thread-safe initialisation.
    struct DelegateStatic
    {
        std::uintptr_t delegate = 0;
        std::uintptr_t guard = 0;
    };

    // Recognises the engine's guarded function-local static getter: a TLS read, `cmp [guard], eax`, then `lea rax, [delegate]; add rsp, n; ret`.
    [[nodiscard]] std::optional<DelegateStatic> ReadSigningKeysGetter(const CodeRegion& text, std::uintptr_t getter);

    // The function that returns the pak signing keys, via the call site upstream's signature matches.
    [[nodiscard]] Located LocateSigningKeysDelegate(const CodeRegion& text);

    // The clang build's delegate binder, with the count global it marks bound. The engine's signature checks are
    // inlined there and read that count; zero means "no signing keys", which is what the getter hook achieves on MSVC.
    struct LocatedBinder
    {
        Located function;
        std::uintptr_t count = 0;
    };

    // The binder that binds the pak signing-keys delegate, for builds whose getter is inlined away. Two functions share
    // its prologue, so the count each marks bound is matched against the one the TOC reader's signature check reads.
    [[nodiscard]] LocatedBinder LocateSigningKeysBinder(const CodeRegion& text, const CodeRegion& rdata);

    // FPakPlatformFile::Unmount: NetEase's wrapper logs "Unmounting pak file" and tail-jumps into it.
    [[nodiscard]] Located LocatePakUnmount(const CodeRegion& text, const CodeRegion& rdata);

    // FPakPlatformFile::Mount: the only code that loads the wide string "_P.pak" for its patch-order check.
    [[nodiscard]] Located LocatePakMount(const CodeRegion& text, const CodeRegion& rdata);
}
