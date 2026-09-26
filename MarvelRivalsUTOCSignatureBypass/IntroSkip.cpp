#include "IntroSkip.h"

#include <algorithm>
#include <span>

namespace bypass
{
    namespace
    {
        constexpr std::wstring_view IntroFolder = L"moviesbink/movies/marvellogo/";

        // ASCII folding is enough: the folder name is ASCII, so any other character is a mismatch either way.
        constexpr wchar_t Fold(wchar_t c) noexcept
        {
            if (c == L'\\') return L'/';
            if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c - L'A' + L'a');
            return c;
        }

        bool IntroFolderAt(std::wstring_view path, std::size_t offset) noexcept
        {
            for (std::size_t i = 0; i < IntroFolder.size(); ++i)
            {
                if (Fold(path[offset + i]) != IntroFolder[i]) return false;
            }
            return true;
        }

        // FNV-1a over the folded path.
        std::uint64_t FoldedHash(std::wstring_view path) noexcept
        {
            std::uint64_t hash = 0xcbf29ce484222325ull;
            for (const wchar_t c : path)
            {
                hash ^= static_cast<std::uint64_t>(Fold(c));
                hash *= 0x100000001b3ull;
            }
            return hash;
        }
    }

    bool IsIntroVideo(std::wstring_view path) noexcept
    {
        if (path.size() < IntroFolder.size()) return false;
        const std::size_t lastStart = path.size() - IntroFolder.size();
        for (std::size_t offset = 0; offset <= lastStart; ++offset)
        {
            const bool segmentStart = offset == 0 || Fold(path[offset - 1]) == L'/';
            if (segmentStart && IntroFolderAt(path, offset)) return true;
        }
        return false;
    }

    bool SeenPaths::Insert(std::wstring_view path) noexcept
    {
        const std::uint64_t hash = FoldedHash(path);
        std::scoped_lock guard(lock_);
        const auto seen = std::span(hashes_).first(count_);
        if (std::ranges::find(seen, hash) != seen.end() || count_ == hashes_.size()) return false;
        hashes_[count_++] = hash;
        return true;
    }
}
