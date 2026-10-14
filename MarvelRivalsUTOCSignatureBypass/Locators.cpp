#include "Locators.h"

#include <cstring>

namespace bypass
{
    namespace
    {
        constexpr std::uint8_t UnmountPrologue[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10 };
        constexpr std::uint8_t JumpOpcode[] = { 0xE9 };
        constexpr std::size_t LeaLength = 7;
        constexpr std::size_t TailJumpWindow = 0x60;
        constexpr std::size_t GetterWindow = 0x40;
        constexpr std::uint8_t ThreadLocalBaseRead[] = { 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00 };
        // push rbp, rbx, rsi, rdi, r12-r15; lea rbp, [rsp+disp32]. The marker sits 0x2e4 bytes in on the 2026-10-09 build.
        constexpr std::uint8_t MountPrologue[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24 };
        constexpr std::size_t MountStartWindow = 0x1000;

        // Destination of the disp32 operand at dispOffset inside the instruction, which is length bytes long.
        std::uintptr_t RipTarget(const CodeRegion& code, std::uintptr_t instruction, std::size_t dispOffset, std::size_t length)
        {
            std::int32_t displacement = 0;
            std::memcpy(&displacement, code.bytes.data() + (instruction - code.base) + dispOffset, sizeof displacement);
            return instruction + length + static_cast<std::intptr_t>(displacement);
        }
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
        case LocateError::TargetOutsideCode: return "call target is outside the code section";
        case LocateError::UnexpectedTarget: return "call target is not the signing-keys getter";
        }
        return "unknown";
    }

    std::optional<DelegateStatic> ReadSigningKeysGetter(const CodeRegion& text, std::uintptr_t getter)
    {
        static const auto guardCompare = BytePattern::Parse("39 05 ?? ?? ?? ??");
        static const auto returnDelegate = BytePattern::Parse("48 8D 05 ?? ?? ?? ?? 48 83 C4 ?? C3");

        if (!text.Contains(getter, GetterWindow)) return std::nullopt;
        const Bytes window = text.bytes.subspan(getter - text.base, GetterWindow);
        if (!FindBytes(window, ThreadLocalBaseRead)) return std::nullopt;

        const auto compares = FindAllPattern(window, *guardCompare);
        const auto returns = FindAllPattern(window, *returnDelegate);
        if (compares.empty() || returns.empty() || compares.front() > returns.front()) return std::nullopt;
        return DelegateStatic{ RipTarget(text, getter + returns.front(), 3, 7), RipTarget(text, getter + compares.front(), 2, 6) };
    }

    Located LocateSigningKeysDelegate(const CodeRegion& text)
    {
        static const auto pattern = BytePattern::Parse("E8 ?? ?? ?? ?? 48 8B F8 39 70 ?? 0F 84 ?? ?? ?? ??");

        const auto matches = FindAllPattern(text.bytes, *pattern);
        if (matches.size() != 1) return { 0, matches.empty() ? LocateError::MarkerMissing : LocateError::Ambiguous };

        const auto target = ResolveCallTarget(text, text.base + matches.front());
        if (!target) return { 0, LocateError::TargetOutsideCode };
        if (text.bytes[*target - text.base] == JumpOpcode[0]) return { 0, LocateError::AlreadyHooked };
        if (!ReadSigningKeysGetter(text, *target)) return { 0, LocateError::UnexpectedTarget };
        return { *target, {} };
    }

    Located LocatePakUnmount(const CodeRegion& text, const CodeRegion& rdata)
    {
        const auto marker = FindWideString(rdata.bytes, L"Unmounting pak file: %s \n");
        if (!marker) return { 0, LocateError::MarkerMissing };

        const auto references = FindLeaReferences(text, rdata.base + *marker, LeaRegister::Rcx);
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

    Located LocatePakMount(const CodeRegion& text, const CodeRegion& rdata)
    {
        const auto marker = FindWideString(rdata.bytes, L"_P.pak");
        if (!marker) return { 0, LocateError::MarkerMissing };

        const auto references = FindLeaReferences(text, rdata.base + *marker, LeaRegister::Rdx);
        if (references.empty()) return { 0, LocateError::ReferenceMissing };

        // Every reference must sit in the same Mount: one elsewhere (an inlined copy, say) could mount paks unrecorded.
        std::optional<std::uintptr_t> mount;
        bool unplaced = false;
        for (const std::uintptr_t reference : references)
        {
            const auto start = FindPaddedFunctionStart(text, reference, MountStartWindow, MountPrologue);
            if (!start) unplaced = true;
            else if (mount && *mount != *start) return { 0, LocateError::Ambiguous };
            else mount = start;
        }
        if (!mount) return { 0, LocateError::PrologueMismatch };
        if (unplaced) return { 0, LocateError::Ambiguous };
        return { *mount, {} };
    }
}
