#include "Locators.h"

namespace bypass
{
    namespace
    {
        constexpr std::uint8_t UnmountPrologue[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10 };
        constexpr std::uint8_t JumpOpcode[] = { 0xE9 };
        constexpr std::size_t LeaLength = 7;
        constexpr std::size_t TailJumpWindow = 0x60;
    }

    const char* Describe(LocateError error) noexcept
    {
        switch (error)
        {
        case LocateError::MarkerMissing: return "marker not found";
        case LocateError::ReferenceMissing: return "no code references the marker";
        case LocateError::PrologueMismatch: return "function start does not match";
        case LocateError::AlreadyHooked: return "already hooked by another plugin";
        case LocateError::Ambiguous: return "pattern matches more than once";
        }
        return "unknown";
    }

    Located LocateSigningKeysDelegate(const CodeRegion& text)
    {
        static const auto pattern = BytePattern::Parse("E8 ?? ?? ?? ?? 48 8B F8 39 70 ?? 0F 84 ?? ?? ?? ??");

        const auto matches = FindAllPattern(text.bytes, *pattern);
        if (matches.size() != 1) return { 0, matches.empty() ? LocateError::MarkerMissing : LocateError::Ambiguous };

        const auto target = ResolveCallTarget(text, text.base + matches.front());
        if (!target) return { 0, LocateError::PrologueMismatch };
        return { *target, {} };
    }

    Located LocatePakUnmount(const CodeRegion& text, const CodeRegion& rdata)
    {
        const auto marker = FindWideString(rdata.bytes, L"Unmounting pak file: %s \n");
        if (!marker) return { 0, LocateError::MarkerMissing };

        const auto references = FindLeaRcxReferences(text, rdata.base + *marker);
        if (references.empty()) return { 0, LocateError::ReferenceMissing };

        for (const std::uintptr_t reference : references)
        {
            if (const auto unmount = FindJumpToPrologue(text, reference + LeaLength, TailJumpWindow, UnmountPrologue))
                return { *unmount, {} };
        }

        // A jump that lands on another jump means something else already patched the function's entry.
        for (const std::uintptr_t reference : references)
        {
            if (FindJumpToPrologue(text, reference + LeaLength, TailJumpWindow, JumpOpcode)) return { 0, LocateError::AlreadyHooked };
        }
        return { 0, LocateError::PrologueMismatch };
    }
}
