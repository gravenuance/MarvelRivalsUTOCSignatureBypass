#include <chrono>
#include <cwchar>
#include <optional>
#include <string>
#include <utility>

#include "../MarvelRivalsUTOCSignatureBypass/ModRemount.h"
#include "Test.h"

using namespace bypass;
using namespace std::chrono_literals;

namespace
{
    const std::wstring ModPak = L"../../../Marvel/Content/Paks/~mods/skins/Storm_9999999_P.pak";
    const MountLog::TimePoint Login{ 1000s };

    MountCall ModMount(std::uint32_t order, std::optional<std::wstring> mountPoint = std::nullopt)
    {
        return { ModPak, order, std::move(mountPoint), true };
    }
}

TEST(RecordedModIsReplayedWithItsOriginalArguments)
{
    MountLog log;
    log.Record(ModMount(4));

    // The unmount reports the path in its own spelling.
    const auto call = log.TakeForRemount(L"..\\..\\..\\Marvel\\Content\\Paks\\~MODS\\skins\\storm_9999999_p.pak", Login);
    CHECK(call.has_value());
    CHECK(call->pakFilename == ModPak);
    CHECK(call->order == 4);
    CHECK(call->loadIndex);
    CHECK(call->MountPointOrNull() == nullptr);
}

TEST(MountPointIsKept)
{
    MountLog log;
    log.Record(ModMount(3, L"../../../Marvel/"));
    const auto call = log.TakeForRemount(ModPak, Login);
    CHECK(call.has_value());
    CHECK(std::wcscmp(call->MountPointOrNull(), L"../../../Marvel/") == 0);
}

TEST(OnlyModsFolderPaksAreRecorded)
{
    MountLog log;
    const std::wstring gamePak = L"../../../Marvel/Content/Paks/pakchunk0-Windows.pak";
    log.Record({ gamePak, 3, std::nullopt, true });
    CHECK(!log.TakeForRemount(gamePak, Login));
}

TEST(UnrecordedPakIsNotReplayed)
{
    MountLog log;
    log.Record(ModMount(4));
    CHECK(!log.TakeForRemount(L"../../../Marvel/Content/Paks/~mods/Other_9999999_P.pak", Login));
}

TEST(UnmountRightAfterARemountIsNotAnsweredAgain)
{
    MountLog log;
    log.Record(ModMount(4));
    CHECK(log.TakeForRemount(ModPak, Login).has_value());
    CHECK(!log.TakeForRemount(ModPak, Login + 1s));
    CHECK(!log.TakeForRemount(ModPak, Login + MountLog::RemountCooldown - 1ms));
}

TEST(LaterLoginIsRemountedAgain)
{
    MountLog log;
    log.Record(ModMount(4));
    CHECK(log.TakeForRemount(ModPak, Login).has_value());

    const auto nextLogin = Login + MountLog::RemountCooldown;
    const auto call = log.TakeForRemount(ModPak, nextLogin);
    CHECK(call.has_value());
    CHECK(call->order == 4);
    CHECK(!log.TakeForRemount(ModPak, nextLogin + 1s));
}

namespace
{
    struct Calls
    {
        int unmounts = 0;
        int remounts = 0;
    };

    bool Handle(ModsAfterLogin mode, bool unmountSucceeds, Calls& calls)
    {
        return HandleCosmeticUnmount(mode,
            [&] { ++calls.unmounts; return unmountSucceeds; },
            [&] { ++calls.remounts; });
    }
}

TEST(KeepRefusesTheUnmount)
{
    Calls calls;
    CHECK(!Handle(ModsAfterLogin::Keep, true, calls));
    CHECK(calls.unmounts == 0);
    CHECK(calls.remounts == 0);
}

TEST(RemountUnmountsThenRemountsAndReportsSuccess)
{
    Calls calls;
    CHECK(Handle(ModsAfterLogin::Remount, true, calls));
    CHECK(calls.unmounts == 1);
    CHECK(calls.remounts == 1);
}

TEST(RemountSkipsTheRemountWhenTheUnmountFails)
{
    Calls calls;
    CHECK(!Handle(ModsAfterLogin::Remount, false, calls));
    CHECK(calls.unmounts == 1);
    CHECK(calls.remounts == 0);
}

TEST(UnloadOnlyUnmounts)
{
    Calls calls;
    CHECK(Handle(ModsAfterLogin::Unload, true, calls));
    CHECK(calls.unmounts == 1);
    CHECK(calls.remounts == 0);
}
