#include "ModRemount.h"

#include <utility>

#include "ModPolicy.h"

namespace bypass
{
    void MountLog::Record(MountCall call)
    {
        if (!IsInModsFolder(call.pakFilename)) return;

        std::wstring key = NormalizePakPath(call.pakFilename);
        std::scoped_lock guard(lock_);
        Entry& entry = entries_[std::move(key)];
        entry.call = std::move(call);
    }

    std::optional<MountCall> MountLog::TakeForRemount(std::wstring_view pakFilename, TimePoint now)
    {
        const std::wstring key = NormalizePakPath(pakFilename);
        std::scoped_lock guard(lock_);
        const auto found = entries_.find(key);
        if (found == entries_.end()) return std::nullopt;

        Entry& entry = found->second;
        if (entry.lastTaken && now - *entry.lastTaken < RemountCooldown) return std::nullopt;
        entry.lastTaken = now;
        return entry.call;
    }
}
