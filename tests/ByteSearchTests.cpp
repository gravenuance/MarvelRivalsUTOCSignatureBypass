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

TEST(FindLeaRcxReferencesResolvesRipRelativeTargets)
{
    Buffer code(64, 0x90);
    const std::uintptr_t base = 0x1000;
    const std::uintptr_t target = 0x800;  // Before the code, so the displacement is negative.

    code[4] = 0x48; code[5] = 0x8D; code[6] = 0x0D;
    PutInt32(code, 7, static_cast<std::int32_t>(target - (base + 4 + 7)));
    code[20] = 0x48; code[21] = 0x8D; code[22] = 0x0D;
    PutInt32(code, 23, 0x1234);
    code[60] = 0x48; code[61] = 0x8D; code[62] = 0x0D;  // Truncated: no room for the displacement.

    const auto references = FindLeaRcxReferences(Region(code, base), target);
    CHECK(references.size() == 1);
    CHECK(references[0] == base + 4);
}

TEST(FindJumpToPrologueRespectsWindowAndRegion)
{
    Buffer code(96, 0xCC);
    const std::uint8_t prologue[] = { 0x48, 0x8B, 0xC4 };
    std::memcpy(code.data() + 80, prologue, sizeof prologue);
    code[10] = 0xE9;
    PutInt32(code, 11, 80 - 15);
    code[30] = 0xE9;
    PutInt32(code, 31, 0x100000);  // Destination outside the region.

    const auto region = Region(code);
    CHECK(FindJumpToPrologue(region, region.base, 32, prologue) == std::optional<std::uintptr_t>(region.base + 80));
    CHECK(!FindJumpToPrologue(region, region.base, 10, prologue));
    CHECK(!FindJumpToPrologue(region, region.base + 20, 32, prologue));
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
