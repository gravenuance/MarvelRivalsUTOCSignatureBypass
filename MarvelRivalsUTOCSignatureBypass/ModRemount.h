#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Settings.h"

namespace bypass
{
    // The arguments of one FPakPlatformFile::Mount call, kept so the same call can be made again.
    struct MountCall
    {
        std::wstring pakFilename;
        std::uint32_t order = 0;
        std::optional<std::wstring> mountPoint;
        bool loadIndex = true;

        [[nodiscard]] const wchar_t* MountPointOrNull() const noexcept { return mountPoint ? mountPoint->c_str() : nullptr; }
    };

    // Remembers how the engine mounted each pak in Paks/~mods, and hands that call back for a remount.
    class MountLog
    {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        // A pak unmounted again this soon after its remount stays unmounted, so a caller that insists wins instead of
        // the two looping. A later login, after this long, is remounted again.
        static constexpr std::chrono::seconds RemountCooldown{ 10 };

        // Paks outside Paks/~mods are ignored. A pak recorded again keeps its cooldown.
        void Record(MountCall call);

        // The recorded call for this pak, unless it was never recorded or was handed out less than the cooldown ago.
        [[nodiscard]] std::optional<MountCall> TakeForRemount(std::wstring_view pakFilename, TimePoint now);

    private:
        struct Entry
        {
            MountCall call;
            std::optional<TimePoint> lastTaken;
        };

        std::mutex lock_;
        std::unordered_map<std::wstring, Entry> entries_;
    };

    // What the unmount hook does with a cosmetic mod: Keep refuses (false), Remount unmounts and, only if that
    // succeeded, remounts and reports success, and Unload just unmounts. The caller sees the returned value.
    template <typename Unmount, typename Remount>
    bool HandleCosmeticUnmount(ModsAfterLogin mode, Unmount&& unmount, Remount&& remount)
    {
        if (mode == ModsAfterLogin::Keep) return false;
        const bool unmounted = unmount();
        if (unmounted && mode == ModsAfterLogin::Remount) remount();
        return unmounted;
    }
}
