#include "ModPolicy.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

namespace bypass
{
    namespace
    {
        std::wstring NormalizeKey(std::wstring_view path)
        {
            std::wstring key(path);
            std::ranges::replace(key, L'\\', L'/');
            std::ranges::transform(key, key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            return key;
        }

        bool IsNormalizedModPath(std::wstring_view normalized) noexcept
        {
            return normalized.find(L"paks/~mods/") != std::wstring_view::npos;
        }

        std::optional<std::vector<std::uint8_t>> ReadUtoc(const std::filesystem::path& utocPath)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(utocPath, error);
            if (error || size > ModPolicy::MaxUtocBytes) return std::nullopt;

            std::ifstream file(utocPath, std::ios::binary);
            if (!file) return std::nullopt;
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (file.gcount() != static_cast<std::streamsize>(bytes.size())) return std::nullopt;
            return bytes;
        }
    }

    std::string_view Describe(UnmountVerdict verdict) noexcept
    {
        switch (verdict)
        {
        case UnmountVerdict::AllowNotAMod: return "not a mod";
        case UnmountVerdict::AllowGameplayMod: return "gameplay mod";
        case UnmountVerdict::KeepCosmeticMod: return "cosmetic mod";
        case UnmountVerdict::KeepUnchecked: return "no .utoc to check";
        }
        return "unknown";
    }

    bool IsInModsFolder(std::wstring_view pakPath) noexcept
    {
        try
        {
            return IsNormalizedModPath(NormalizeKey(pakPath));
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
    }

    bool HasGameplayContent(Bytes utoc)
    {
        return ContainsIgnoreCase(utoc, "AbilitySystem") || ContainsIgnoreCase(utoc, "CameraShake");
    }

    ModPolicy::ModPolicy(std::filesystem::path baseDirectory) : baseDirectory_(std::move(baseDirectory))
    {
    }

    UnmountVerdict ModPolicy::Decide(std::wstring_view pakPath)
    {
        const std::wstring key = NormalizeKey(pakPath);
        {
            std::shared_lock read(cacheLock_);
            if (const auto found = cache_.find(key); found != cache_.end()) return found->second;
        }

        const UnmountVerdict verdict = Classify(pakPath, key);
        std::unique_lock write(cacheLock_);
        cache_.emplace(key, verdict);
        return verdict;
    }

    UnmountVerdict ModPolicy::Classify(std::wstring_view pakPath, std::wstring_view normalizedPath) const
    {
        if (!IsNormalizedModPath(normalizedPath)) return UnmountVerdict::AllowNotAMod;

        std::filesystem::path path(pakPath);
        if (path.is_relative()) path = baseDirectory_ / path;
        path.replace_extension(L".utoc");

        // A pak without a readable .utoc is kept, as Galacta does: there is nothing to show it changes gameplay.
        const auto utoc = ReadUtoc(path);
        if (!utoc) return UnmountVerdict::KeepUnchecked;
        return HasGameplayContent(*utoc) ? UnmountVerdict::AllowGameplayMod : UnmountVerdict::KeepCosmeticMod;
    }
}
