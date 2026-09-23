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

TEST(LocateSigningKeysNeedsExactlyOneMatch)
{
    const std::vector<std::uint8_t> callSite = { 0xE8, 0x10, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF8, 0x39, 0x70, 0x08, 0x0F, 0x84, 0x01, 0x02, 0x03, 0x04 };

    Buffer one(0x80, 0xCC);
    std::memcpy(one.data() + 0x20, callSite.data(), callSite.size());
    const Located found = LocateSigningKeysDelegate({ Bytes(one.data(), one.size()), TextBase });
    CHECK(found.Found());
    CHECK(found.address == TextBase + 0x20 + 5 + 0x10);

    Buffer none(0x80, 0xCC);
    CHECK(LocateSigningKeysDelegate({ Bytes(none.data(), none.size()), TextBase }).error == LocateError::MarkerMissing);

    Buffer outside = one;
    outside[0x21] = 0x00; outside[0x22] = 0x10;  // call rel32 = 0x1000, past the end of the region
    CHECK(LocateSigningKeysDelegate({ Bytes(outside.data(), outside.size()), TextBase }).error == LocateError::TargetOutsideCode);

    Buffer two = one;
    std::memcpy(two.data() + 0x50, callSite.data(), callSite.size());
    CHECK(LocateSigningKeysDelegate({ Bytes(two.data(), two.size()), TextBase }).error == LocateError::Ambiguous);
}
