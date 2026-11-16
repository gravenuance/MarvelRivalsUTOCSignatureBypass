#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../MarvelRivalsUTOCSignatureBypass/Locators.h"
#include "Test.h"

using namespace bypass;

namespace
{
    using Buffer = std::vector<std::uint8_t>;

    constexpr std::uintptr_t TextBase = 0x10000;
    constexpr std::uintptr_t RdataBase = 0x80000;
    constexpr std::size_t MarkerOffset = 16;
    constexpr std::size_t LeaOffset = 0x20;
    constexpr std::size_t JumpOffset = 0x40;
    constexpr std::size_t UnmountOffset = 0x100;

    void PutInt32(Buffer& buffer, std::size_t offset, std::int32_t value)
    {
        std::memcpy(buffer.data() + offset, &value, sizeof value);
    }

    Buffer RdataWithMarker(const std::wstring& marker)
    {
        Buffer rdata(MarkerOffset, 0x00);
        for (const wchar_t c : marker) { rdata.push_back(static_cast<std::uint8_t>(c)); rdata.push_back(0); }
        rdata.insert(rdata.end(), { 0, 0 });
        return rdata;
    }

    // Mirrors the game: `lea rcx, [marker]` inside the NetEase wrapper, then `jmp` into FPakPlatformFile::Unmount.
    Buffer UnmountWrapperCode(bool withReference, const std::vector<std::uint8_t>& unmountStart)
    {
        Buffer text(0x200, 0xCC);
        if (withReference)
        {
            text[LeaOffset] = 0x48; text[LeaOffset + 1] = 0x8D; text[LeaOffset + 2] = 0x0D;
            PutInt32(text, LeaOffset + 3, static_cast<std::int32_t>((RdataBase + MarkerOffset) - (TextBase + LeaOffset + 7)));
        }
        text[JumpOffset] = 0xE9;
        PutInt32(text, JumpOffset + 1, static_cast<std::int32_t>(UnmountOffset - (JumpOffset + 5)));
        std::memcpy(text.data() + UnmountOffset, unmountStart.data(), unmountStart.size());
        return text;
    }

    const std::vector<std::uint8_t> RealPrologue = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10 };
    // The 2026-11-14 clang/LLVM build opens Unmount with twelve pushes instead.
    const std::vector<std::uint8_t> ClangPrologue = {
        0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53 };
    const std::wstring Marker = L"Unmounting pak file: %s \n";

    Located Locate(const Buffer& text, const Buffer& rdata)
    {
        return LocatePakUnmount({ Bytes(text.data(), text.size()), TextBase }, { Bytes(rdata.data(), rdata.size()), RdataBase });
    }
}

TEST(LocatePakUnmountFollowsWrapperToPrologue)
{
    const Located found = Locate(UnmountWrapperCode(true, RealPrologue), RdataWithMarker(Marker));
    CHECK(found.Found());
    CHECK(found.address == TextBase + UnmountOffset);
}

TEST(LocatePakUnmountFollowsWrapperToClangPrologue)
{
    const Located found = Locate(UnmountWrapperCode(true, ClangPrologue), RdataWithMarker(Marker));
    CHECK(found.Found());
    CHECK(found.address == TextBase + UnmountOffset);
}

TEST(LocatePakUnmountReportsMissingMarker)
{
    const Located found = Locate(UnmountWrapperCode(true, RealPrologue), RdataWithMarker(L"Mounting pak file: %s \n"));
    CHECK(!found.Found());
    CHECK(found.error == LocateError::MarkerMissing);
}

TEST(LocatePakUnmountReportsMissingReference)
{
    const Located found = Locate(UnmountWrapperCode(false, RealPrologue), RdataWithMarker(Marker));
    CHECK(found.error == LocateError::ReferenceMissing);
}

TEST(LocatePakUnmountRejectsChangedPrologue)
{
    const Located found = Locate(UnmountWrapperCode(true, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x90 }), RdataWithMarker(Marker));
    CHECK(found.error == LocateError::PrologueMismatch);
}

TEST(LocatePakUnmountDetectsAnotherPluginsHook)
{
    const Located found = Locate(UnmountWrapperCode(true, { 0xE9, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90 }), RdataWithMarker(Marker));
    CHECK(found.error == LocateError::AlreadyHooked);
}

namespace
{
    constexpr std::size_t MountOffset = 0x40;
    constexpr std::size_t MountMarkerLea = MountOffset + 0x2e4;  // Where the 2026-10-09 build loads "_P.pak".
    constexpr std::size_t ClangMountMarkerLea = MountOffset + 0x1988;  // Where the 2026-11-14 build loads it.

    const std::vector<std::uint8_t> RealMountPrologue = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x08, 0xFE, 0xFF, 0xFF };
    const std::vector<std::uint8_t> ClangMountPrologue = {
        0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xE8, 0x05, 0x00, 0x00 };

    // First three bytes of `lea rdx, [rip+disp32]` (MSVC builds) and of `lea r8, [rip+disp32]` (clang builds).
    constexpr std::uint8_t LeaRdx[3] = { 0x48, 0x8D, 0x15 };
    constexpr std::uint8_t LeaR8[3] = { 0x4C, 0x8D, 0x05 };

    void PutMarkerLea(Buffer& text, std::size_t at, const std::uint8_t (&lea)[3])
    {
        std::memcpy(text.data() + at, lea, sizeof lea);
        PutInt32(text, at + 3, static_cast<std::int32_t>((RdataBase + MarkerOffset) - (TextBase + at + 7)));
    }

    // Mirrors the game: int3 padding, Mount's prologue, a body with lone 0xCC bytes, and the `lea` that loads "_P.pak".
    Buffer MountCode(const std::vector<std::uint8_t>& prologue, bool withReference,
        std::size_t markerAt = MountMarkerLea, const std::uint8_t (&lea)[3] = LeaRdx)
    {
        Buffer text(std::max<std::size_t>(0x400, markerAt + 0x40), 0x90);
        std::fill_n(text.begin() + MountOffset - 4, 4, std::uint8_t{ 0xCC });
        std::memcpy(text.data() + MountOffset, prologue.data(), prologue.size());
        text[MountOffset + 0x80] = 0xCC;
        text[MountOffset + 0x1c0] = 0xCC;
        if (withReference) PutMarkerLea(text, markerAt, lea);
        return text;
    }

    Located LocateMount(const Buffer& text, const Buffer& rdata)
    {
        return LocatePakMount({ Bytes(text.data(), text.size()), TextBase }, { Bytes(rdata.data(), rdata.size()), RdataBase });
    }
}

TEST(LocatePakMountWalksBackFromMarkerToPrologue)
{
    const Located found = LocateMount(MountCode(RealMountPrologue, true), RdataWithMarker(L"_P.pak"));
    CHECK(found.Found());
    CHECK(found.address == TextBase + MountOffset);
}

TEST(LocatePakMountWalksBackFromAnR8MarkerToTheClangPrologue)
{
    const Buffer text = MountCode(ClangMountPrologue, true, ClangMountMarkerLea, LeaR8);
    const Located found = LocateMount(text, RdataWithMarker(L"_P.pak"));
    CHECK(found.Found());
    CHECK(found.address == TextBase + MountOffset);
}

TEST(LocatePakMountRejectsReferencesInTwoFunctionsAcrossRegisters)
{
    // One reference per register in two different functions still has to be refused.
    Buffer text = MountCode(RealMountPrologue, true);
    constexpr std::size_t secondFunction = 0x340;
    std::fill_n(text.begin() + secondFunction - 2, 2, std::uint8_t{ 0xCC });
    std::memcpy(text.data() + secondFunction, ClangMountPrologue.data(), ClangMountPrologue.size());
    PutMarkerLea(text, secondFunction + 0x40, LeaR8);
    CHECK(LocateMount(text, RdataWithMarker(L"_P.pak")).error == LocateError::Ambiguous);
}

TEST(LocatePakMountReportsMissingMarkerAndReference)
{
    CHECK(LocateMount(MountCode(RealMountPrologue, true), RdataWithMarker(L".pak")).error == LocateError::MarkerMissing);
    CHECK(LocateMount(MountCode(RealMountPrologue, false), RdataWithMarker(L"_P.pak")).error == LocateError::ReferenceMissing);
}

TEST(LocatePakMountRejectsChangedOrHookedPrologue)
{
    const Located changed = LocateMount(MountCode({ 0x48, 0x89, 0x5C, 0x24, 0x08 }, true), RdataWithMarker(L"_P.pak"));
    CHECK(changed.error == LocateError::PrologueMismatch);
    const Located hooked = LocateMount(MountCode({ 0xE9, 0x00, 0x00, 0x00, 0x00 }, true), RdataWithMarker(L"_P.pak"));
    CHECK(hooked.error == LocateError::PrologueMismatch);
}

TEST(LocatePakMountRejectsReferencesInTwoFunctions)
{
    Buffer text = MountCode(RealMountPrologue, true);
    constexpr std::size_t secondFunction = 0x340;
    std::fill_n(text.begin() + secondFunction - 2, 2, std::uint8_t{ 0xCC });
    std::memcpy(text.data() + secondFunction, RealMountPrologue.data(), RealMountPrologue.size());
    PutMarkerLea(text, secondFunction + 0x40, LeaRdx);
    CHECK(LocateMount(text, RdataWithMarker(L"_P.pak")).error == LocateError::Ambiguous);
}

TEST(LocatePakMountRejectsAReferenceItCannotPlace)
{
    // A second reference in a function with another prologue, such as a copy of Mount inlined into a caller.
    Buffer text = MountCode(RealMountPrologue, true);
    constexpr std::size_t inliningCaller = 0x340;
    std::fill_n(text.begin() + inliningCaller - 2, 2, std::uint8_t{ 0xCC });
    text[inliningCaller] = 0x48; text[inliningCaller + 1] = 0x89; text[inliningCaller + 2] = 0x5C;
    PutMarkerLea(text, inliningCaller + 0x40, LeaRdx);
    CHECK(LocateMount(text, RdataWithMarker(L"_P.pak")).error == LocateError::Ambiguous);
}

namespace
{
    // The game's getter at 0x140f0d590 (build d6ec510b), placed at its real address so its RIP operands resolve as in the game.
    const std::vector<std::uint8_t> RealSigningKeysGetter = {
        0x48, 0x83, 0xEC, 0x28,                                // sub rsp, 28h
        0x8B, 0x0D, 0x76, 0xBC, 0xE9, 0x0E,                    // mov ecx, [_tls_index]
        0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00,  // mov rax, gs:[58h]
        0xBA, 0xB4, 0x27, 0x00, 0x00,                          // mov edx, 27B4h
        0x48, 0x8B, 0x04, 0xC8,                                // mov rax, [rax+rcx*8]
        0x8B, 0x04, 0x02,                                      // mov eax, [rdx+rax]
        0x39, 0x05, 0x1B, 0xB3, 0x93, 0x0E,                    // cmp [guard], eax
        0x7F, 0x0C,                                            // jg init
        0x48, 0x8D, 0x05, 0x02, 0xB3, 0x93, 0x0E,              // lea rax, [delegate]
        0x48, 0x83, 0xC4, 0x28,                                // add rsp, 28h
        0xC3,                                                  // ret
        0x48, 0x8D, 0x0D, 0x06, 0xB3, 0x93, 0x0E,              // init: lea rcx, [guard]
        0xE8, 0x41, 0x89, 0x1F, 0x09,                          // call _Init_thread_header
        0x83, 0x3D, 0xFA, 0xB2, 0x93, 0x0E, 0xFF,              // cmp [guard], -1
        0x75, 0xDF,                                            // jne back to the fast path
        0x48, 0x8D, 0x0D, 0xC1, 0xDD, 0x28, 0x09,              // lea rcx, [destructor]
        0xE8, 0x44, 0x87, 0x1F, 0x09,                          // call atexit
        0x48, 0x8D, 0x0D, 0xE5, 0xB2, 0x93, 0x0E,              // lea rcx, [guard]
        0xE8, 0xB4, 0x88, 0x1F, 0x09,                          // call _Init_thread_footer
        0x48, 0x8D, 0x05, 0xC9, 0xB2, 0x93, 0x0E,              // lea rax, [delegate]
        0x48, 0x83, 0xC4, 0x28,                                // add rsp, 28h
        0xC3,                                                  // ret
    };

    // The same getter with the other guard compare seen in guarded-static getters, `cmp dword [guard], -1`. It is one
    // byte longer than `cmp [guard], eax`, so the displacements after it are shifted to resolve to the same statics.
    // The initialisation tail is left off: it sits past the window the getter is read through.
    const std::vector<std::uint8_t> MinusOneGuardGetter = {
        0x48, 0x83, 0xEC, 0x28,                                // sub rsp, 28h
        0x8B, 0x0D, 0x76, 0xBC, 0xE9, 0x0E,                    // mov ecx, [_tls_index]
        0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00,  // mov rax, gs:[58h]
        0xBA, 0xB4, 0x27, 0x00, 0x00,                          // mov edx, 27B4h
        0x48, 0x8B, 0x04, 0xC8,                                // mov rax, [rax+rcx*8]
        0x8B, 0x04, 0x02,                                      // mov eax, [rdx+rax]
        0x83, 0x3D, 0x1A, 0xB3, 0x93, 0x0E, 0xFF,              // cmp dword [guard], -1
        0x75, 0x0C,                                            // jne init
        0x48, 0x8D, 0x05, 0x01, 0xB3, 0x93, 0x0E,              // lea rax, [delegate]
        0x48, 0x83, 0xC4, 0x28,                                // add rsp, 28h
        0xC3,                                                  // ret
    };

    constexpr std::size_t CallSiteOffset = 0x20;
    constexpr std::size_t GetterOffset = 0x80;
    constexpr std::uintptr_t RealGetterAddress = 0x140F0D590;
    constexpr std::uintptr_t SigningTextBase = RealGetterAddress - GetterOffset;

    // Upstream's call-site pattern, calling the getter at GetterOffset.
    void PutCallSite(Buffer& text, std::size_t offset)
    {
        const std::vector<std::uint8_t> callSite = { 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF8, 0x39, 0x70, 0x08, 0x0F, 0x84, 0x01, 0x02, 0x03, 0x04 };
        std::memcpy(text.data() + offset, callSite.data(), callSite.size());
        PutInt32(text, offset + 1, static_cast<std::int32_t>(GetterOffset - (offset + 5)));
    }

    Buffer SigningKeysCode(const std::vector<std::uint8_t>& getter)
    {
        Buffer text(0x100, 0xCC);
        PutCallSite(text, CallSiteOffset);
        std::memcpy(text.data() + GetterOffset, getter.data(), getter.size());
        return text;
    }

    CodeRegion Region(const Buffer& text)
    {
        return { Bytes(text.data(), text.size()), SigningTextBase };
    }
}

TEST(LocateSigningKeysNeedsExactlyOneMatch)
{
    const Buffer one = SigningKeysCode(RealSigningKeysGetter);
    const Located found = LocateSigningKeysDelegate(Region(one));
    CHECK(found.Found());
    CHECK(found.address == RealGetterAddress);

    Buffer none(0x100, 0xCC);
    CHECK(LocateSigningKeysDelegate(Region(none)).error == LocateError::MarkerMissing);

    Buffer outside = one;
    PutInt32(outside, CallSiteOffset + 1, 0x1000);
    CHECK(LocateSigningKeysDelegate(Region(outside)).error == LocateError::TargetOutsideCode);

    Buffer two = one;
    PutCallSite(two, 0x50);
    CHECK(LocateSigningKeysDelegate(Region(two)).error == LocateError::Ambiguous);
}

TEST(ReadSigningKeysGetterFindsDelegateAndGuard)
{
    const Buffer text = SigningKeysCode(RealSigningKeysGetter);
    const auto getter = ReadSigningKeysGetter(Region(text), RealGetterAddress);
    CHECK(getter.has_value());
    CHECK(getter->delegate == 0x14F8488C0);
    CHECK(getter->guard == 0x14F8488D0);
}

TEST(ReadSigningKeysGetterAcceptsAGuardComparedAgainstMinusOne)
{
    const Buffer text = SigningKeysCode(MinusOneGuardGetter);
    const auto getter = ReadSigningKeysGetter(Region(text), RealGetterAddress);
    CHECK(getter.has_value());
    CHECK(getter->delegate == 0x14F8488C0);
    CHECK(getter->guard == 0x14F8488D0);
}

TEST(LocateSigningKeysRejectsTargetThatIsNotTheGetter)
{
    CHECK(LocateSigningKeysDelegate(Region(SigningKeysCode({ 0xCC }))).error == LocateError::UnexpectedTarget);

    auto noThreadLocalRead = RealSigningKeysGetter;
    noThreadLocalRead[10] = 0x90;  // gs prefix gone
    CHECK(LocateSigningKeysDelegate(Region(SigningKeysCode(noThreadLocalRead))).error == LocateError::UnexpectedTarget);

    auto noGuardCompare = RealSigningKeysGetter;
    noGuardCompare[31] = 0x3B;  // cmp eax, [guard]: the operands the other way round
    CHECK(LocateSigningKeysDelegate(Region(SigningKeysCode(noGuardCompare))).error == LocateError::UnexpectedTarget);

    auto noStaticReturned = RealSigningKeysGetter;
    noStaticReturned[39] = 0x8B;  // mov rax, [delegate]: returns the value, not the static's address
    CHECK(LocateSigningKeysDelegate(Region(SigningKeysCode(noStaticReturned))).error == LocateError::UnexpectedTarget);
}

TEST(LocateSigningKeysDetectsAnotherPluginsHook)
{
    auto hooked = RealSigningKeysGetter;
    hooked[0] = 0xE9;
    CHECK(LocateSigningKeysDelegate(Region(SigningKeysCode(hooked))).error == LocateError::AlreadyHooked);
}

namespace
{
    // The 2026-11-14 build's addresses, so every displacement below spans the distance it spans in the game.
    constexpr std::uintptr_t BinderTextBase = 0x140001000;
    constexpr std::uintptr_t BinderRdataBase = 0x143000000;
    // The count that build's signature check reads, and the global one byte below it: resolving the check's
    // `cmp dword [rip+count], 0` as if the trailing immediate were not part of the instruction lands on that one.
    constexpr std::uintptr_t CheckedCount = 0x15475F8B8;
    constexpr std::uintptr_t NeighbourCount = CheckedCount - 1;

    constexpr std::size_t FirstBinderOffset = 0x100;
    constexpr std::size_t SecondBinderOffset = 0x200;
    constexpr std::size_t StringLoadOffset = 0x400;
    constexpr std::size_t CountCheckOffset = 0x440;
    constexpr std::size_t BinderCountOperand = 0x2D;  // `mov eax, [rip+count]`, six bytes long.

    // The game's binder at 0x1410e4f90, up to the count it tests; the displacements are filled in per placement.
    const std::vector<std::uint8_t> BinderPrologue = {
        0x56,                                                  // push rsi
        0x48, 0x83, 0xEC, 0x20,                                // sub rsp, 20h
        0x48, 0x89, 0xCE,                                      // mov rsi, rcx
        0x8B, 0x05, 0, 0, 0, 0,                                // mov eax, [guard]
        0x8B, 0x0D, 0, 0, 0, 0,                                // mov ecx, [_tls_index]
        0x65, 0x48, 0x8B, 0x14, 0x25, 0x58, 0x00, 0x00, 0x00,  // mov rdx, gs:[58h]
        0x48, 0x8B, 0x0C, 0xCA,                                // mov rcx, [rdx+rcx*8]
        0x3B, 0x81, 0x48, 0x01, 0x00, 0x00,                    // cmp eax, [rcx+148h]
        0x0F, 0x8F, 0, 0, 0, 0,                                // jg lazy init
        0x8B, 0x05, 0, 0, 0, 0,                                // mov eax, [count]
        0x85, 0xC0,                                            // test eax, eax
        0x74, 0x1E,                                            // je bind
        0x48, 0x8B, 0x0D, 0, 0, 0, 0,                          // mov rcx, [object]
    };

    void PutBinder(Buffer& text, std::size_t at, std::uintptr_t count)
    {
        std::memcpy(text.data() + at, BinderPrologue.data(), BinderPrologue.size());
        const std::uintptr_t after = BinderTextBase + at + BinderCountOperand + 6;
        PutInt32(text, at + BinderCountOperand + 2, static_cast<std::int32_t>(count - after));
    }

    // `cmp dword [rip+count], 0`, the shape the engine's inlined signature checks use.
    void PutCountCheck(Buffer& text, std::size_t at, std::uintptr_t count)
    {
        text[at] = 0x83; text[at + 1] = 0x3D; text[at + 6] = 0x00;
        PutInt32(text, at + 2, static_cast<std::int32_t>(count - (BinderTextBase + at + 7)));
    }

    // `vmovdqu ymm0, [rip+disp32]`: the clang build loads the wide marker with an AVX move, not a lea.
    void PutStringLoad(Buffer& text, std::size_t at)
    {
        text[at] = 0xC5; text[at + 1] = 0xFE; text[at + 2] = 0x6F; text[at + 3] = 0x05;
        PutInt32(text, at + 4, static_cast<std::int32_t>((BinderRdataBase + MarkerOffset) - (BinderTextBase + at + 8)));
    }

    // Mirrors the game: two functions sharing the binder's prologue, and a TOC reader that loads the marker and tests
    // one count global twice.
    Buffer BinderCode(std::uintptr_t firstCount, std::uintptr_t secondCount, std::uintptr_t checkedCount,
        bool withStringLoad = true)
    {
        Buffer text(0x800, 0xCC);
        PutBinder(text, FirstBinderOffset, firstCount);
        PutBinder(text, SecondBinderOffset, secondCount);
        if (withStringLoad) PutStringLoad(text, StringLoadOffset);
        PutCountCheck(text, CountCheckOffset, checkedCount);
        PutCountCheck(text, CountCheckOffset + 0x20, checkedCount);
        return text;
    }

    LocatedBinder LocateBinder(const Buffer& text, const Buffer& rdata)
    {
        return LocateSigningKeysBinder({ Bytes(text.data(), text.size()), BinderTextBase },
            { Bytes(rdata.data(), rdata.size()), BinderRdataBase });
    }
}

TEST(LocateSigningKeysBinderPicksTheCandidateWhoseCountTheCheckReads)
{
    // The first candidate's count is the one a six-byte read of the check would resolve to, so taking either the first
    // match or the wrong instruction length picks the wrong function.
    const Buffer text = BinderCode(NeighbourCount, CheckedCount, CheckedCount);
    const LocatedBinder found = LocateBinder(text, RdataWithMarker(L"Missing signature"));
    CHECK(found.function.Found());
    CHECK(found.function.address == BinderTextBase + SecondBinderOffset);
    CHECK(found.count == CheckedCount);
}

TEST(LocateSigningKeysBinderRefusesWhenNoCandidateMarksTheCheckedCount)
{
    const Buffer text = BinderCode(CheckedCount + 0x100, CheckedCount + 0x200, CheckedCount);
    const LocatedBinder found = LocateBinder(text, RdataWithMarker(L"Missing signature"));
    CHECK(!found.function.Found());
    CHECK(found.function.error == LocateError::Ambiguous);
    CHECK(found.count == 0);
}

TEST(LocateSigningKeysBinderRefusesWhenTwoCandidatesMarkTheCheckedCount)
{
    const Buffer text = BinderCode(CheckedCount, CheckedCount, CheckedCount);
    CHECK(LocateBinder(text, RdataWithMarker(L"Missing signature")).function.error == LocateError::Ambiguous);
}

TEST(LocateSigningKeysBinderReportsAMissingPrologueMarkerAndReference)
{
    const Buffer none(0x800, 0xCC);
    CHECK(LocateBinder(none, RdataWithMarker(L"Missing signature")).function.error == LocateError::MarkerMissing);

    const Buffer text = BinderCode(NeighbourCount, CheckedCount, CheckedCount);
    CHECK(LocateBinder(text, RdataWithMarker(L"Missing signatures")).function.error == LocateError::MarkerMissing);

    const Buffer noLoad = BinderCode(NeighbourCount, CheckedCount, CheckedCount, false);
    CHECK(LocateBinder(noLoad, RdataWithMarker(L"Missing signature")).function.error == LocateError::ReferenceMissing);
}
