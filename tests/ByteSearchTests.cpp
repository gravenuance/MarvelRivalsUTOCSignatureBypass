#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../MarvelRivalsUTOCSignatureBypass/ByteSearch.h"
#include "Test.h"

using namespace bypass;

namespace
{
    using Buffer = std::vector<std::uint8_t>;

    void PutInt32(Buffer& buffer, std::size_t offset, std::int32_t value)
    {
        std::memcpy(buffer.data() + offset, &value, sizeof value);
    }

    CodeRegion Region(const Buffer& buffer, std::uintptr_t base = 0x1000)
    {
        return { Bytes(buffer.data(), buffer.size()), base };
    }
}

TEST(PatternParseRejectsMalformedText)
{
    CHECK(!BytePattern::Parse("E8 GG"));
    CHECK(!BytePattern::Parse("E"));
    CHECK(!BytePattern::Parse("?? ??"));
    CHECK(!BytePattern::Parse(""));
}

TEST(PatternAnchorIsLongestLiteralRun)
{
    const auto pattern = BytePattern::Parse("E8 ?? 48 8B F8 ?? 0F");
    CHECK(pattern);
    CHECK(pattern->AnchorOffset() == 2);
    CHECK(pattern->Anchor().size() == 3);
    CHECK(pattern->Size() == 7);
}

TEST(FindAllPatternHonoursWildcardsAndBoundaries)
{
    const Buffer haystack = { 0xAA, 0xBB, 0x11, 0xCC, 0x00, 0xAA, 0xBB, 0x22, 0xCC, 0xAA, 0xBB };
    const auto pattern = BytePattern::Parse("AA BB ?? CC");
    const auto matches = FindAllPattern(haystack, *pattern);
    CHECK(matches.size() == 2);
    CHECK(matches[0] == 0);
    CHECK(matches[1] == 5);
}

TEST(FindAllPatternFindsLiteralRunsAcrossVectorBoundaries)
{
    // Placements on both sides of every 16-byte step and in the scalar tail, which the vector scan handles separately.
    for (std::size_t size = 3; size <= 70; ++size)
    {
        for (std::size_t at = 0; at + 3 <= size; ++at)
        {
            Buffer haystack(size, 0x48);
            haystack[at] = 0x48; haystack[at + 1] = 0x8D; haystack[at + 2] = 0x0D;
            const auto matches = FindAllPattern(haystack, *BytePattern::Parse("48 8D 0D"));
            CHECK(matches.size() == 1);
            CHECK(matches[0] == at);
        }
    }
}

TEST(FindAllPatternVerifiesWildcardsAroundLongAnchor)
{
    const Buffer haystack = { 0xE8, 1, 2, 3, 4, 0x48, 0x8B, 0xF8, 0x00, 0x48, 0x8B, 0xF8, 0xE8, 9, 9, 9, 9, 0x48, 0x8B, 0xF8 };
    const auto matches = FindAllPattern(haystack, *BytePattern::Parse("E8 ?? ?? ?? ?? 48 8B F8"));
    CHECK(matches.size() == 2);
    CHECK(matches[0] == 0);
    CHECK(matches[1] == 12);
}

TEST(FindAllPatternIgnoresMatchTruncatedAtEnd)
{
    const Buffer haystack = { 0x00, 0xAA, 0xBB };
    CHECK(FindAllPattern(haystack, *BytePattern::Parse("AA BB ??")).empty());
}

TEST(FindBytesStartingPastEndFindsNothing)
{
    const Buffer haystack = { 1, 2, 3 };
    const Buffer needle = { 3 };
    CHECK(FindBytes(haystack, needle, 2) == std::optional<std::size_t>(2));
    CHECK(!FindBytes(haystack, needle, 4));
    CHECK(!FindBytes(haystack, {}, 0));
}

TEST(ContainsIgnoreCaseMatchesAnyCase)
{
    const std::string text = "Marvel/Content/Marvel/AbilitySystem/GA_Test";
    const Bytes bytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    CHECK(ContainsIgnoreCase(bytes, "abilitysystem"));
    CHECK(ContainsIgnoreCase(bytes, "ABILITYSYSTEM"));
    CHECK(!ContainsIgnoreCase(bytes, "CameraShake"));
    CHECK(ContainsIgnoreCase(bytes, ""));
    CHECK(!ContainsIgnoreCase(Bytes(), "a"));
}

TEST(FindWideStringNeedsTerminatorAndAlignment)
{
    Buffer data = { 0x00 };  // Shifts the first copy to an odd offset.
    for (const wchar_t c : std::wstring(L"Hi")) { data.push_back(static_cast<std::uint8_t>(c)); data.push_back(0); }
    data.insert(data.end(), { 0, 0, 0 });
    for (const wchar_t c : std::wstring(L"Hi!")) { data.push_back(static_cast<std::uint8_t>(c)); data.push_back(0); }
    data.insert(data.end(), { 0, 0 });

    CHECK(!FindWideString(data, L"Hi"));
    CHECK(FindWideString(data, L"Hi!") == std::optional<std::size_t>(8));
}

TEST(FindWideStringSkipsTheTailOfALongerString)
{
    Buffer data;
    for (const wchar_t c : std::wstring(L"ab_P.pak")) { data.push_back(static_cast<std::uint8_t>(c)); data.push_back(0); }
    data.insert(data.end(), { 0, 0 });
    for (const wchar_t c : std::wstring(L"_P.pak")) { data.push_back(static_cast<std::uint8_t>(c)); data.push_back(0); }
    data.insert(data.end(), { 0, 0 });

    CHECK(FindWideString(data, L"_P.pak") == std::optional<std::size_t>(18));
    CHECK(FindWideString(data, L"ab_P.pak") == std::optional<std::size_t>(0));
}

TEST(FindLeaReferencesResolvesRipRelativeTargets)
{
    Buffer code(80, 0x90);
    const std::uintptr_t base = 0x1000;
    const std::uintptr_t target = 0x800;  // Before the code, so the displacement is negative.

    code[4] = 0x48; code[5] = 0x8D; code[6] = 0x0D;  // lea rcx
    PutInt32(code, 7, static_cast<std::int32_t>(target - (base + 4 + 7)));
    code[20] = 0x48; code[21] = 0x8D; code[22] = 0x0D;
    PutInt32(code, 23, 0x1234);
    code[40] = 0x48; code[41] = 0x8D; code[42] = 0x15;  // lea rdx, same target
    PutInt32(code, 43, static_cast<std::int32_t>(target - (base + 40 + 7)));
    code[56] = 0x4C; code[57] = 0x8D; code[58] = 0x05;  // lea r8, same target: REX.R, so a different ModRM byte
    PutInt32(code, 59, static_cast<std::int32_t>(target - (base + 56 + 7)));
    code[76] = 0x48; code[77] = 0x8D; code[78] = 0x0D;  // Truncated: no room for the displacement.

    const auto rcx = FindLeaReferences(Region(code, base), target, LeaRegister::Rcx);
    CHECK(rcx.size() == 1);
    CHECK(rcx[0] == base + 4);
    const auto rdx = FindLeaReferences(Region(code, base), target, LeaRegister::Rdx);
    CHECK(rdx.size() == 1);
    CHECK(rdx[0] == base + 40);
    const auto r8 = FindLeaReferences(Region(code, base), target, LeaRegister::R8);
    CHECK(r8.size() == 1);
    CHECK(r8[0] == base + 56);
}

TEST(FindRipReferencesMeasuresFromTheEndOfTheInstruction)
{
    Buffer code(40, 0x90);
    const std::uintptr_t base = 0x1000;
    const std::uintptr_t target = 0x2000;
    // `cmp dword [rip+disp32], 0`: the displacement is followed by the immediate, so it resolves seven bytes on.
    const auto compare = BytePattern::Parse("83 3D ?? ?? ?? ?? 00");

    code[4] = 0x83; code[5] = 0x3D; code[10] = 0x00;
    PutInt32(code, 6, static_cast<std::int32_t>(target - (base + 4 + 7)));
    code[20] = 0x83; code[21] = 0x3D; code[26] = 0x00;
    PutInt32(code, 22, static_cast<std::int32_t>(target - (base + 20 + 6)));  // Measured from the displacement's end.

    const auto references = FindRipReferences(Region(code, base), *compare, 2, target);
    CHECK(references.size() == 1);
    CHECK(references[0] == base + 4);
    // The second site really reads one byte past target, which is the global a six-byte read would have found.
    CHECK(FindRipReferences(Region(code, base), *compare, 2, target + 1).size() == 1);
    CHECK(FindRipReferences(Region(code, base), *compare, 4, target).empty());
}

TEST(FindPaddedFunctionStartStopsAtNearestPadding)
{
    Buffer code(96, 0x90);
    const std::uint8_t prologue[] = { 0x40, 0x55, 0x53 };
    const std::array<Bytes, 1> prologues = { Bytes(prologue) };
    code[14] = 0xCC; code[15] = 0xCC;
    std::memcpy(code.data() + 16, prologue, sizeof prologue);  // Function start, 16-byte aligned like MSVC's.
    code[24] = 0xCC;                   // A lone 0xCC inside an instruction is not a boundary,
    code[37] = 0xCC; code[38] = 0xCC;  // and neither is a double one (an imm32 of 0xCCCCCCCC) before an unaligned address.

    const auto region = Region(code);
    CHECK(FindPaddedFunctionStart(region, region.base + 60, 60, prologues) == std::optional<std::uintptr_t>(region.base + 16));
    CHECK(FindPaddedFunctionStart(region, region.base + 16, 0, prologues) == std::optional<std::uintptr_t>(region.base + 16));
    CHECK(!FindPaddedFunctionStart(region, region.base + 60, 43, prologues));
    CHECK(!FindPaddedFunctionStart(region, region.base + 200, 300, prologues));

    // A later function whose prologue changed: the walk stops at its padding instead of reaching the one before it.
    code[46] = 0xCC; code[47] = 0xCC; code[48] = 0x48;
    CHECK(!FindPaddedFunctionStart(region, region.base + 60, 60, prologues));
}

TEST(FindPaddedFunctionStartAcceptsAnyKnownPrologue)
{
    Buffer code(96, 0x90);
    const std::uint8_t msvc[] = { 0x40, 0x55, 0x53 };
    const std::uint8_t clang[] = { 0x41, 0x57, 0x41 };
    const std::array<Bytes, 2> prologues = { Bytes(msvc), Bytes(clang) };
    code[14] = 0xCC; code[15] = 0xCC;
    std::memcpy(code.data() + 16, clang, sizeof clang);  // The second form still has to be recognised.

    const auto region = Region(code);
    CHECK(FindPaddedFunctionStart(region, region.base + 60, 60, prologues) == std::optional<std::uintptr_t>(region.base + 16));
    CHECK(!FindPaddedFunctionStart(region, region.base + 60, 60, std::array<Bytes, 1>{ Bytes(msvc) }));
}

TEST(FindJumpToPrologueRespectsWindowAndRegion)
{
    Buffer code(96, 0xCC);
    const std::uint8_t prologue[] = { 0x48, 0x8B, 0xC4 };
    const std::array<Bytes, 1> prologues = { Bytes(prologue) };
    std::memcpy(code.data() + 80, prologue, sizeof prologue);
    code[10] = 0xE9;
    PutInt32(code, 11, 80 - 15);
    code[30] = 0xE9;
    PutInt32(code, 31, 0x100000);  // Destination outside the region.

    const auto region = Region(code);
    CHECK(FindJumpToPrologue(region, region.base, 32, prologues) == std::optional<std::uintptr_t>(region.base + 80));
    CHECK(!FindJumpToPrologue(region, region.base, 10, prologues));
    CHECK(!FindJumpToPrologue(region, region.base + 20, 32, prologues));
}

TEST(FindJumpToPrologueAcceptsAnyKnownPrologue)
{
    Buffer code(96, 0xCC);
    const std::uint8_t msvc[] = { 0x48, 0x8B, 0xC4 };
    const std::uint8_t clang[] = { 0x41, 0x57, 0x41 };
    std::memcpy(code.data() + 80, clang, sizeof clang);
    code[10] = 0xE9;
    PutInt32(code, 11, 80 - 15);

    const auto region = Region(code);
    const std::array<Bytes, 2> prologues = { Bytes(msvc), Bytes(clang) };
    CHECK(FindJumpToPrologue(region, region.base, 32, prologues) == std::optional<std::uintptr_t>(region.base + 80));
    CHECK(!FindJumpToPrologue(region, region.base, 32, std::array<Bytes, 1>{ Bytes(msvc) }));
}

TEST(ResolveCallTargetChecksOpcodeAndRange)
{
    Buffer code(32, 0x90);
    code[0] = 0xE8;
    PutInt32(code, 1, 10);
    code[8] = 0xE8;
    PutInt32(code, 9, 0x7FFFFF);

    const auto region = Region(code);
    CHECK(ResolveCallTarget(region, region.base) == std::optional<std::uintptr_t>(region.base + 15));
    CHECK(!ResolveCallTarget(region, region.base + 1));
    CHECK(!ResolveCallTarget(region, region.base + 8));
    CHECK(!ResolveCallTarget(region, region.base + 30));
}
