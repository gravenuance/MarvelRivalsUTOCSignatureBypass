#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bypass
{
    using Bytes = std::span<const std::uint8_t>;

    // A code region together with the address its first byte lives at, so RIP-relative operands can be resolved.
    struct CodeRegion
    {
        Bytes bytes;
        std::uintptr_t base = 0;

        [[nodiscard]] bool Contains(std::uintptr_t address, std::size_t length = 1) const noexcept
        {
            return address >= base && address - base <= bytes.size() && bytes.size() - (address - base) >= length;
        }
    };

    // Byte pattern parsed from text such as "E8 ?? ?? ?? ?? 48 8B F8"; "??" is a wildcard.
    class BytePattern
    {
    public:
        static std::optional<BytePattern> Parse(std::string_view text);

        [[nodiscard]] std::size_t Size() const noexcept { return bytes_.size(); }
        [[nodiscard]] bool MatchesAt(Bytes haystack, std::size_t offset) const noexcept;

        // Longest run of literal bytes, used as the fast search anchor.
        [[nodiscard]] std::size_t AnchorOffset() const noexcept { return anchorOffset_; }
        [[nodiscard]] Bytes Anchor() const noexcept { return { anchor_.data(), anchor_.size() }; }

    private:
        std::vector<std::optional<std::uint8_t>> bytes_;
        std::vector<std::uint8_t> anchor_;
        std::size_t anchorOffset_ = 0;
    };

    [[nodiscard]] std::optional<std::size_t> FindBytes(Bytes haystack, Bytes needle, std::size_t from = 0);
    [[nodiscard]] std::vector<std::size_t> FindAllPattern(Bytes haystack, const BytePattern& pattern);
    [[nodiscard]] bool ContainsIgnoreCase(Bytes haystack, std::string_view needle);

    // Offset of the wide string (including its terminator) inside a data region.
    [[nodiscard]] std::optional<std::size_t> FindWideString(Bytes data, std::wstring_view text);

    // Addresses of every `lea rcx, [rip+disp32]` whose operand resolves to target.
    [[nodiscard]] std::vector<std::uintptr_t> FindLeaRcxReferences(const CodeRegion& code, std::uintptr_t target);

    // First `jmp rel32` in the window after `from` whose destination starts with the expected prologue.
    [[nodiscard]] std::optional<std::uintptr_t> FindJumpToPrologue(
        const CodeRegion& code, std::uintptr_t from, std::size_t window, Bytes prologue);

    // Destination of the `call rel32` at address.
    [[nodiscard]] std::optional<std::uintptr_t> ResolveCallTarget(const CodeRegion& code, std::uintptr_t address);
}
