#include "ByteSearch.h"

#include <algorithm>
#include <cstring>
#include <functional>

namespace bypass
{
    namespace
    {
        std::optional<std::uint8_t> ParseHexByte(std::string_view token)
        {
            if (token.size() != 2) return std::nullopt;
            std::uint8_t value = 0;
            for (const char c : token)
            {
                value = static_cast<std::uint8_t>(value << 4);
                if (c >= '0' && c <= '9') value = static_cast<std::uint8_t>(value | (c - '0'));
                else if (c >= 'a' && c <= 'f') value = static_cast<std::uint8_t>(value | (c - 'a' + 10));
                else if (c >= 'A' && c <= 'F') value = static_cast<std::uint8_t>(value | (c - 'A' + 10));
                else return std::nullopt;
            }
            return value;
        }

        std::int32_t ReadInt32(Bytes bytes, std::size_t offset) noexcept
        {
            std::int32_t value = 0;
            std::memcpy(&value, bytes.data() + offset, sizeof value);
            return value;
        }

        char ToLowerAscii(std::uint8_t c) noexcept
        {
            return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        }
    }

    std::optional<BytePattern> BytePattern::Parse(std::string_view text)
    {
        BytePattern pattern;
        std::size_t position = 0;
        while (position < text.size())
        {
            if (text[position] == ' ') { ++position; continue; }
            const std::string_view token = text.substr(position, 2);
            position += token.size();
            if (token == "??") { pattern.bytes_.emplace_back(std::nullopt); continue; }
            const auto value = ParseHexByte(token);
            if (!value) return std::nullopt;
            pattern.bytes_.emplace_back(value);
        }

        std::size_t runStart = 0;
        std::size_t bestStart = 0;
        std::size_t bestLength = 0;
        for (std::size_t i = 0; i <= pattern.bytes_.size(); ++i)
        {
            if (i < pattern.bytes_.size() && pattern.bytes_[i]) continue;
            if (i - runStart > bestLength) { bestStart = runStart; bestLength = i - runStart; }
            runStart = i + 1;
        }
        if (bestLength == 0) return std::nullopt;

        pattern.anchorOffset_ = bestStart;
        for (std::size_t i = bestStart; i < bestStart + bestLength; ++i) pattern.anchor_.push_back(*pattern.bytes_[i]);
        return pattern;
    }

    bool BytePattern::MatchesAt(Bytes haystack, std::size_t offset) const noexcept
    {
        if (offset > haystack.size() || haystack.size() - offset < bytes_.size()) return false;
        for (std::size_t i = 0; i < bytes_.size(); ++i)
        {
            if (bytes_[i] && *bytes_[i] != haystack[offset + i]) return false;
        }
        return true;
    }

    std::optional<std::size_t> FindBytes(Bytes haystack, Bytes needle, std::size_t from)
    {
        if (needle.empty() || from > haystack.size()) return std::nullopt;
        const std::boyer_moore_horspool_searcher searcher(needle.begin(), needle.end());
        const auto found = std::search(haystack.begin() + static_cast<std::ptrdiff_t>(from), haystack.end(), searcher);
        if (found == haystack.end()) return std::nullopt;
        return static_cast<std::size_t>(found - haystack.begin());
    }

    std::vector<std::size_t> FindAllPattern(Bytes haystack, const BytePattern& pattern)
    {
        std::vector<std::size_t> matches;
        const Bytes anchor = pattern.Anchor();
        std::size_t from = pattern.AnchorOffset();
        while (const auto anchorAt = FindBytes(haystack, anchor, from))
        {
            const std::size_t start = *anchorAt - pattern.AnchorOffset();
            if (pattern.MatchesAt(haystack, start)) matches.push_back(start);
            from = *anchorAt + 1;
        }
        return matches;
    }

    bool ContainsIgnoreCase(Bytes haystack, std::string_view needle)
    {
        if (needle.empty()) return true;
        const auto equal = [](std::uint8_t a, char b) { return ToLowerAscii(a) == ToLowerAscii(static_cast<std::uint8_t>(b)); };
        return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), equal) != haystack.end();
    }

    std::optional<std::size_t> FindWideString(Bytes data, std::wstring_view text)
    {
        std::vector<std::uint8_t> encoded;
        encoded.reserve((text.size() + 1) * 2);
        for (const wchar_t c : text)
        {
            encoded.push_back(static_cast<std::uint8_t>(c & 0xFF));
            encoded.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
        }
        encoded.insert(encoded.end(), { 0, 0 });

        // UTF-16 literals are 2-byte aligned, so an odd hit is a false match straddling two characters.
        std::size_t from = 0;
        while (const auto found = FindBytes(data, encoded, from))
        {
            if (*found % 2 == 0) return found;
            from = *found + 1;
        }
        return std::nullopt;
    }

    std::vector<std::uintptr_t> FindLeaRcxReferences(const CodeRegion& code, std::uintptr_t target)
    {
        static constexpr std::uint8_t leaRcxRip[] = { 0x48, 0x8D, 0x0D };
        constexpr std::size_t instructionLength = 7;

        std::vector<std::uintptr_t> references;
        std::size_t from = 0;
        while (const auto found = FindBytes(code.bytes, leaRcxRip, from))
        {
            from = *found + 1;
            if (code.bytes.size() - *found < instructionLength) break;
            const std::uintptr_t next = code.base + *found + instructionLength;
            if (next + static_cast<std::intptr_t>(ReadInt32(code.bytes, *found + 3)) == target)
                references.push_back(code.base + *found);
        }
        return references;
    }

    std::optional<std::uintptr_t> FindJumpToPrologue(
        const CodeRegion& code, std::uintptr_t from, std::size_t window, Bytes prologue)
    {
        constexpr std::uint8_t jmpRel32 = 0xE9;
        constexpr std::size_t jumpLength = 5;

        for (std::uintptr_t at = from; at < from + window && code.Contains(at, jumpLength); ++at)
        {
            const std::size_t offset = at - code.base;
            if (code.bytes[offset] != jmpRel32) continue;
            const std::uintptr_t destination = at + jumpLength + static_cast<std::intptr_t>(ReadInt32(code.bytes, offset + 1));
            if (!code.Contains(destination, prologue.size())) continue;
            if (std::equal(prologue.begin(), prologue.end(), code.bytes.begin() + static_cast<std::ptrdiff_t>(destination - code.base)))
                return destination;
        }
        return std::nullopt;
    }

    std::optional<std::uintptr_t> ResolveCallTarget(const CodeRegion& code, std::uintptr_t address)
    {
        constexpr std::uint8_t callRel32 = 0xE8;
        constexpr std::size_t callLength = 5;

        if (!code.Contains(address, callLength) || code.bytes[address - code.base] != callRel32) return std::nullopt;
        const std::uintptr_t destination = address + callLength + static_cast<std::intptr_t>(ReadInt32(code.bytes, address - code.base + 1));
        if (!code.Contains(destination)) return std::nullopt;
        return destination;
    }
}
