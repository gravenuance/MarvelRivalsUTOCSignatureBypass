#pragma once
#include <chrono>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ModPolicy.h"
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

    // What the Mount hook does: calls the engine's Mount, records the call if the pak is in Paks/~mods, and returns
    // Mount's result unchanged. The call is recorded even when Mount returns false: for a pak with no .utoc (audio
    // mods, typically) the engine adds the pak to its mounted list, then reports false because it finds no IoStore
    // container. A pak that really failed to mount is never unmounted, so its record is never used.
    // A failure to record goes to onRecordError, and Mount's result is still returned.
    template <typename Log, typename Mount, typename OnRecordError>
    bool MountAndRecord(Log& log, const wchar_t* pakFilename, std::uint32_t order, const wchar_t* mountPoint, bool loadIndex,
        Mount&& mount, OnRecordError&& onRecordError)
    {
        const bool mounted = mount();
        // Checked before building the record, so the game's own paks cost no allocation.
        if (pakFilename == nullptr || !IsInModsFolder(pakFilename)) return mounted;
        try
        {
            log.Record({ pakFilename, order, mountPoint ? std::optional<std::wstring>(mountPoint) : std::nullopt, loadIndex });
        }
        catch (const std::exception& error)
        {
            onRecordError(error);
        }
        return mounted;
    }

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
