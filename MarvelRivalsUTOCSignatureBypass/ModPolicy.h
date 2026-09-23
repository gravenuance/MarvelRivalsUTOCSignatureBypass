#pragma once
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ByteSearch.h"

namespace bypass
{
    enum class UnmountVerdict
    {
        AllowNotAMod,
        AllowGameplayMod,
        KeepCosmeticMod,
        KeepUnchecked,
    };

    [[nodiscard]] std::string_view Describe(UnmountVerdict verdict) noexcept;
    [[nodiscard]] constexpr bool KeepsMounted(UnmountVerdict verdict) noexcept
    {
        return verdict == UnmountVerdict::KeepCosmeticMod || verdict == UnmountVerdict::KeepUnchecked;
    }

    // True for any path inside a Paks/~mods folder, in either slash style and any letter case.
    [[nodiscard]] bool IsInModsFolder(std::wstring_view pakPath) noexcept;

    // Galacta's rule: a container listing ability or camera-shake assets can change gameplay, so it is not protected.
    [[nodiscard]] bool HasGameplayContent(Bytes utoc);

    // Decides whether an unmount request may proceed; results are cached per pak because the .utoc is read from disk.
    class ModPolicy
    {
    public:
        // Relative pak paths from the engine are resolved against this directory (the game executable's folder).
        explicit ModPolicy(std::filesystem::path baseDirectory);

        [[nodiscard]] UnmountVerdict Decide(std::wstring_view pakPath);

        static constexpr std::uintmax_t MaxUtocBytes = 64ull * 1024 * 1024;

    private:
        [[nodiscard]] UnmountVerdict Classify(std::wstring_view pakPath) const;

        std::filesystem::path baseDirectory_;
        std::shared_mutex cacheLock_;
        std::unordered_map<std::wstring, UnmountVerdict> cache_;
    };
}
