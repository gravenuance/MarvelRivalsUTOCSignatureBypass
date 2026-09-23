#pragma once
#include <cstdint>

#include "ByteSearch.h"

namespace bypass
{
    enum class LocateError
    {
        MarkerMissing,
        ReferenceMissing,
        PrologueMismatch,
        AlreadyHooked,
        Ambiguous,
        TargetOutsideCode,
    };

    struct Located
    {
        std::uintptr_t address = 0;
        LocateError error = LocateError::MarkerMissing;
        [[nodiscard]] bool Found() const noexcept { return address != 0; }
    };

    [[nodiscard]] const char* Describe(LocateError error) noexcept;

    // The function that returns the pak signing keys, via the call site upstream's signature matches.
    [[nodiscard]] Located LocateSigningKeysDelegate(const CodeRegion& text);

    // FPakPlatformFile::Unmount: NetEase's wrapper logs "Unmounting pak file" and tail-jumps into it.
    [[nodiscard]] Located LocatePakUnmount(const CodeRegion& text, const CodeRegion& rdata);
}
