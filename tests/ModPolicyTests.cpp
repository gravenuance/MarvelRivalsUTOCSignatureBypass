#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>

#include "../MarvelRivalsUTOCSignatureBypass/ModPolicy.h"
#include "Test.h"

using namespace bypass;
namespace fs = std::filesystem;

namespace
{
    // One folder per test and process, so tests stay independent when run in parallel.
    class GameFolder
    {
    public:
        explicit GameFolder(const char* testName)
            : root_(fs::temp_directory_path() / (std::string("mrusb-") + testName + "-" + std::to_string(_getpid())))
        {
            fs::remove_all(root_);
            fs::create_directories(Mods());
        }
        ~GameFolder() { std::error_code ignored; fs::remove_all(root_, ignored); }
        GameFolder(const GameFolder&) = delete;
        GameFolder& operator=(const GameFolder&) = delete;

        [[nodiscard]] const fs::path& Root() const { return root_; }
        [[nodiscard]] fs::path Mods() const { return root_ / "Paks" / "~mods"; }

        void WriteUtoc(const std::string& pakName, const std::string& contents) const
        {
            std::ofstream(Mods() / (pakName + ".utoc"), std::ios::binary) << contents;
        }

    private:
        fs::path root_;
    };

    std::wstring PakIn(const fs::path& folder, const std::string& pakName)
    {
        return (folder / (pakName + ".pak")).wstring();
    }
}

TEST(ModsFolderMatchesEitherSlashAndAnyCase)
{
    CHECK(IsInModsFolder(L"../../../Marvel/Content/Paks/~mods/zSkin_P.pak"));
    CHECK(IsInModsFolder(L"D:\\Games\\Marvel\\Content\\PAKS\\~Mods\\pimpi\\zSkin_P.pak"));
    CHECK(!IsInModsFolder(L"../../../Marvel/Content/Paks/pakchunk0-Windows.pak"));
    CHECK(!IsInModsFolder(L"../../../Marvel/Content/Paks/~modsbackup/zSkin_P.pak"));
    CHECK(!IsInModsFolder(L""));
}

TEST(GameplayContentIsAbilityOrCameraShake)
{
    const auto bytesOf = [](const std::string& text) { return Bytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()); };
    const std::string ability = "Marvel/Content/Marvel/AbilitySystem/GA_Dash";
    const std::string shake = "Marvel/Content/Marvel/Camera/cameraSHAKE_Hit";
    const std::string skin = "Marvel/Content/Marvel/Characters/1011/Meshes/SK_Body";
    CHECK(HasGameplayContent(bytesOf(ability)));
    CHECK(HasGameplayContent(bytesOf(shake)));
    CHECK(!HasGameplayContent(bytesOf(skin)));
}

TEST(OnlyModVerdictsKeepThePakMounted)
{
    CHECK(!KeepsMounted(UnmountVerdict::AllowNotAMod));
    CHECK(!KeepsMounted(UnmountVerdict::AllowGameplayMod));
    CHECK(KeepsMounted(UnmountVerdict::KeepCosmeticMod));
    CHECK(KeepsMounted(UnmountVerdict::KeepUnchecked));
    for (const auto verdict : { UnmountVerdict::AllowNotAMod, UnmountVerdict::AllowGameplayMod, UnmountVerdict::KeepCosmeticMod, UnmountVerdict::KeepUnchecked })
        CHECK(Describe(verdict) != "unknown");
}

TEST(CosmeticModIsKeptMounted)
{
    const GameFolder game("cosmetic");
    game.WriteUtoc("zSkin_P", "Characters/1011/Meshes/SK_Body");
    ModPolicy policy(game.Root());
    CHECK(policy.Decide(PakIn(game.Mods(), "zSkin_P")) == UnmountVerdict::KeepCosmeticMod);
}

TEST(GameplayModMayBeUnmounted)
{
    const GameFolder game("gameplay");
    game.WriteUtoc("zCheat_P", "Marvel/AbilitySystem/GA_Dash");
    ModPolicy policy(game.Root());
    CHECK(policy.Decide(PakIn(game.Mods(), "zCheat_P")) == UnmountVerdict::AllowGameplayMod);
}

TEST(ModWithoutUtocIsKeptMounted)
{
    const GameFolder game("noutoc");
    ModPolicy policy(game.Root());
    CHECK(policy.Decide(PakIn(game.Mods(), "zLegacy_P")) == UnmountVerdict::KeepUnchecked);
}

TEST(RelativePakPathResolvesAgainstGameFolder)
{
    const GameFolder game("relative");
    game.WriteUtoc("zCheat_P", "CameraShake");
    ModPolicy policy(game.Root());
    CHECK(policy.Decide(L"Paks/~mods/zCheat_P.pak") == UnmountVerdict::AllowGameplayMod);
}

TEST(BaseGamePakIsNotAMod)
{
    const GameFolder game("basegame");
    ModPolicy policy(game.Root());
    CHECK(policy.Decide(PakIn(game.Root() / "Paks", "pakchunk0-Windows")) == UnmountVerdict::AllowNotAMod);
}

TEST(VerdictIsCachedPerPak)
{
    const GameFolder game("cache");
    game.WriteUtoc("zSkin_P", "Characters/SK_Body");
    ModPolicy policy(game.Root());
    const std::wstring pak = PakIn(game.Mods(), "zSkin_P");
    CHECK(policy.Decide(pak) == UnmountVerdict::KeepCosmeticMod);

    game.WriteUtoc("zSkin_P", "AbilitySystem");
    CHECK(policy.Decide(pak) == UnmountVerdict::KeepCosmeticMod);
}
