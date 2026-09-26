#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace bypass
{
    // True for any path inside a MoviesBink/Movies/MarvelLogo folder (the startup logo videos), in either slash style and any letter case.
    [[nodiscard]] bool IsIntroVideo(std::wstring_view path) noexcept;

    // Remembers paths without allocating, ignoring slash style and letter case; once full, nothing more counts as new.
    class SeenPaths
    {
    public:
        // True the first time a path is seen.
        [[nodiscard]] bool Insert(std::wstring_view path) noexcept;

        static constexpr std::size_t Capacity = 64;

    private:
        std::mutex lock_;
        std::array<std::uint64_t, Capacity> hashes_{};
        std::size_t count_ = 0;
    };
}
