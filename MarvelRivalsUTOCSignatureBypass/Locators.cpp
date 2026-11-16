#include "Locators.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace bypass
{
    namespace
    {
        constexpr std::uint8_t JumpOpcode[] = { 0xE9 };
        // A function whose first byte is a jump has had its entry patched, so a jump is the "prologue" to look for.
        constexpr std::array<Bytes, 1> JumpPrologue = { Bytes(JumpOpcode) };
        constexpr std::size_t LeaLength = 7;
        constexpr std::size_t TailJumpWindow = 0x60;
        constexpr std::size_t GetterWindow = 0x40;
        constexpr std::uint8_t ThreadLocalBaseRead[] = { 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00 };

        // The game has shipped from two compilers: MSVC up to 2026-11-13, clang/LLVM from 2026-11-14. Both forms are
        // tried so one plugin build works on either game build.
        // MSVC: `mov rax, rsp; mov [rax+10h], rdx`. clang: push r15, r14, r13, r12, rsi, rdi, rbp, rbx.
        constexpr std::uint8_t UnmountPrologueMsvc[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10 };
        constexpr std::uint8_t UnmountPrologueClang[] = { 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53 };
        constexpr std::array<Bytes, 2> UnmountPrologues = { Bytes(UnmountPrologueMsvc), Bytes(UnmountPrologueClang) };

        // MSVC: push rbp, rbx, rsi, rdi, r12-r15; lea rbp, [rsp+disp32]. clang: the same pushes as Unmount, then
        // `sub rsp, 5e8h`. The marker sits 0x2e4 bytes in on the 2026-10-09 build and 0x1988 in on the 2026-11-14 one.
        constexpr std::uint8_t MountPrologueMsvc[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24 };
        constexpr std::uint8_t MountPrologueClang[] = {
            0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xE8, 0x05, 0x00, 0x00 };
        constexpr std::array<Bytes, 2> MountPrologues = { Bytes(MountPrologueMsvc), Bytes(MountPrologueClang) };
        constexpr std::size_t MountStartWindow = 0x2800;

        // Where the binder's `mov eax, [rip+count]` sits, and the lengths the RIP operands around it are measured
        // from: a displacement is relative to the end of its instruction, and `cmp dword [rip+count], 0` ends one
        // byte past its displacement.
        constexpr std::size_t BinderCountOperand = 0x2D;
        constexpr std::size_t CountReadLength = 6;
        constexpr std::size_t CountCheckLength = 7;
        constexpr std::size_t MarkerLoadDisplacement = 4;
        // Half the span searched for the signature check around the marker: the TOC reader is about 0x1700 bytes.
        constexpr std::size_t SignatureCheckWindow = 0x1000;

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
        case LocateError::PrologueMissing: return "no function starts with the expected prologue";
        case LocateError::PrologueMismatch: return "function start does not match";
        case LocateError::AlreadyHooked: return "already hooked by another plugin";
        case LocateError::Ambiguous: return "pattern matches more than once";
        case LocateError::CountUnmatched: return "no candidate marks the delegate size the check reads";
        case LocateError::TargetOutsideCode: return "call target is outside the code section";
        case LocateError::UnexpectedTarget: return "call target is not the signing-keys getter";
        }
        return "unknown";
    }

    std::optional<DelegateStatic> ReadSigningKeysGetter(const CodeRegion& text, std::uintptr_t getter)
    {
        // `cmp [guard], eax`, the only form either shipped build uses in the guard position: the MSVC getter's
        // `cmp dword [guard], -1` belongs to its lazy-init path and falls outside GetterWindow, and the clang build
        // reaches the keys through the binder instead. Accepting only this form keeps the check before a detour tight.
        static const auto guardCompare = BytePattern::Parse("39 05 ?? ?? ?? ??");
        constexpr std::size_t GuardCompareLength = 6;
        static const auto returnDelegate = BytePattern::Parse("48 8D 05 ?? ?? ?? ?? 48 83 C4 ?? C3");

        if (!text.Contains(getter, GetterWindow)) return std::nullopt;
        const Bytes window = text.bytes.subspan(getter - text.base, GetterWindow);
        if (!FindBytes(window, ThreadLocalBaseRead)) return std::nullopt;

        const auto compares = FindAllPattern(window, *guardCompare);
        const auto returns = FindAllPattern(window, *returnDelegate);
        if (compares.empty() || returns.empty() || compares.front() > returns.front()) return std::nullopt;
        return DelegateStatic{ RipTarget(text, getter + returns.front(), 3, 7),
            RipTarget(text, getter + compares.front(), 2, GuardCompareLength) };
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

    LocatedBinder LocateSigningKeysBinder(const CodeRegion& text, const CodeRegion& rdata)
    {
        // The binder: `push rsi; sub rsp, 20h; mov rsi, rcx`, the guarded-static preamble, then the count it marks
        // bound. Two functions share it, so a match alone identifies nothing.
        static const auto binder = BytePattern::Parse(
            "56 48 83 EC 20 48 89 CE 8B 05 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 65 48 8B 14 25 58 00 00 00 "
            "48 8B 0C CA 3B 81 48 01 00 00 0F 8F ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? 85 C0 74 1E 48 8B 0D ?? ?? ?? ??");
        // clang loads the wide marker into a vector register, so there is no `lea` on it to follow.
        static const auto markerLoad = BytePattern::Parse("C5 FE 6F 05 ?? ?? ?? ??");
        static const auto countCheck = BytePattern::Parse("83 3D ?? ?? ?? ?? 00");

        // The marker lookups come first: either can fail, and they cost a fraction of the binder scan over all of .text.
        const auto marker = FindWideString(rdata.bytes, L"Missing signature");
        if (!marker) return { { 0, LocateError::MarkerMissing } };
        const auto references = FindRipReferences(text, *markerLoad, MarkerLoadDisplacement, rdata.base + *marker);
        if (references.empty()) return { { 0, LocateError::ReferenceMissing } };

        // Every count the signature check tests around the marker, that is, inside the TOC reader that logs it.
        std::vector<std::uintptr_t> checked;
        for (const std::uintptr_t reference : references)
        {
            // Clamped at both ends: deriving the length from the window instead would reach 2 * SignatureCheckWindow
            // forward for a marker load near the start of .text, past the function the span is meant to cover.
            const std::size_t at = reference - text.base;
            const std::size_t start = at > SignatureCheckWindow ? at - SignatureCheckWindow : 0;
            const std::size_t end = std::min(text.bytes.size(), at + SignatureCheckWindow);
            const CodeRegion window{ text.bytes.subspan(start, end - start), text.base + start };
            for (const std::size_t site : FindAllPattern(window.bytes, *countCheck))
                checked.push_back(RipTarget(window, window.base + site, 2, CountCheckLength));
        }

        const auto matches = FindAllPattern(text.bytes, *binder);
        if (matches.empty()) return { { 0, LocateError::PrologueMissing } };

        // The binder to hook is the one whose count the check reads; anything else is a sibling delegate's binder.
        LocatedBinder found{};
        for (const std::size_t match : matches)
        {
            const std::uintptr_t count = RipTarget(text, text.base + match + BinderCountOperand, 2, CountReadLength);
            if (std::find(checked.begin(), checked.end(), count) == checked.end()) continue;
            if (found.function.Found()) return { { 0, LocateError::Ambiguous } };
            found = { { text.base + match, {} }, count };
        }
        if (!found.function.Found()) return { { 0, LocateError::CountUnmatched } };
        return found;
    }

    Located LocatePakUnmount(const CodeRegion& text, const CodeRegion& rdata)
    {
        const auto marker = FindWideString(rdata.bytes, L"Unmounting pak file: %s \n");
        if (!marker) return { 0, LocateError::MarkerMissing };

        const auto references = FindLeaReferences(text, rdata.base + *marker, LeaRegister::Rcx);
        if (references.empty()) return { 0, LocateError::ReferenceMissing };

        for (const std::uintptr_t reference : references)
        {
            if (const auto unmount = FindJumpToPrologue(text, reference + LeaLength, TailJumpWindow, UnmountPrologues))
                return { *unmount, {} };
        }

        // A jump that lands on another jump means something else already patched the function's entry.
        for (const std::uintptr_t reference : references)
        {
            if (FindJumpToPrologue(text, reference + LeaLength, TailJumpWindow, JumpPrologue)) return { 0, LocateError::AlreadyHooked };
        }
        return { 0, LocateError::PrologueMismatch };
    }

    Located LocatePakMount(const CodeRegion& text, const CodeRegion& rdata)
    {
        const auto marker = FindWideString(rdata.bytes, L"_P.pak");
        if (!marker) return { 0, LocateError::MarkerMissing };

        // MSVC builds pass the marker in rdx, clang builds in r8; a reference in either is one the game can mount from.
        std::vector<std::uintptr_t> references = FindLeaReferences(text, rdata.base + *marker, LeaRegister::Rdx);
        const auto inR8 = FindLeaReferences(text, rdata.base + *marker, LeaRegister::R8);
        references.insert(references.end(), inR8.begin(), inR8.end());
        if (references.empty()) return { 0, LocateError::ReferenceMissing };

        // Every reference must sit in the same Mount: one elsewhere (an inlined copy, say) could mount paks unrecorded.
        std::optional<std::uintptr_t> mount;
        bool unplaced = false;
        for (const std::uintptr_t reference : references)
        {
            const auto start = FindPaddedFunctionStart(text, reference, MountStartWindow, MountPrologues);
            if (!start) unplaced = true;
            else if (mount && *mount != *start) return { 0, LocateError::Ambiguous };
            else mount = start;
        }
        if (!mount) return { 0, LocateError::PrologueMismatch };
        if (unplaced) return { 0, LocateError::Ambiguous };
        return { *mount, {} };
    }
}
