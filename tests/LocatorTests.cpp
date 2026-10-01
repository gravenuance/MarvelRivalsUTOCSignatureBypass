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
