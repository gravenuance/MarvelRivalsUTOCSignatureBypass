#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>

#include "../MarvelRivalsUTOCSignatureBypass/Settings.h"
#include "Test.h"

using namespace bypass;
namespace fs = std::filesystem;

namespace
{
    // One file per test and process, so tests stay independent when run in parallel.
    class IniFile
    {
    public:
        explicit IniFile(const char* testName)
            : path_(fs::temp_directory_path() / (std::string("mrusb-") + testName + "-" + std::to_string(_getpid()) + ".ini"))
        {
            fs::remove(path_);
        }
        ~IniFile() { std::error_code ignored; fs::remove(path_, ignored); }
        IniFile(const IniFile&) = delete;
        IniFile& operator=(const IniFile&) = delete;

        [[nodiscard]] const fs::path& Path() const { return path_; }

        void Write(const std::string& contents) const
        {
            std::ofstream(path_, std::ios::binary) << contents;
        }

    private:
        fs::path path_;
    };
}

TEST(SettingsMissingFileSkipsIntroVideos)
{
    const IniFile ini("SettingsMissing");
    const LoadedSettings loaded = LoadSettings(ini.Path());
    CHECK(loaded.settings.introVideos == IntroVideos::Skip);
    CHECK(loaded.warnings.empty());
}

TEST(SettingsReadSkipIntroVideos)
{
    const IniFile ini("SettingsRead");
    ini.Write("[Settings]\r\nVersion=1\r\nSkipIntroVideos=0\r\n");
    CHECK(LoadSettings(ini.Path()).settings.introVideos == IntroVideos::Play);

    ini.Write("[Settings]\r\nVersion=1\r\nSkipIntroVideos=1\r\n");
    CHECK(LoadSettings(ini.Path()).settings.introVideos == IntroVideos::Skip);
    CHECK(LoadSettings(ini.Path()).warnings.empty());
}

TEST(SettingsMissingKeysUseDefaults)
{
    const IniFile ini("SettingsKeys");
    ini.Write("[Settings]\r\nVersion=1\r\n");
    CHECK(LoadSettings(ini.Path()).settings.introVideos == IntroVideos::Skip);
    CHECK(LoadSettings(ini.Path()).warnings.empty());

    ini.Write("[Settings]\r\nSkipIntroVideos=0\r\n");
    CHECK(LoadSettings(ini.Path()).settings.introVideos == IntroVideos::Play);
    CHECK(LoadSettings(ini.Path()).warnings.empty());
}

TEST(SettingsInvalidValueWarnsAndUsesDefault)
{
    const IniFile ini("SettingsInvalid");
    ini.Write("[Settings]\r\nVersion=1\r\nSkipIntroVideos=no\r\n");
    const LoadedSettings loaded = LoadSettings(ini.Path());
    CHECK(loaded.settings.introVideos == IntroVideos::Skip);
    CHECK(loaded.warnings.size() == 1);
    CHECK(loaded.warnings[0].find("SkipIntroVideos=no") != std::string::npos);
}

TEST(SettingsUnknownVersionWarnsAndUsesDefaults)
{
    const IniFile ini("SettingsVersion");
    for (const char* version : { "2", "0", "one", "99999999999" })
    {
        ini.Write(std::string("[Settings]\r\nVersion=") + version + "\r\nSkipIntroVideos=0\r\n");
        const LoadedSettings loaded = LoadSettings(ini.Path());
        CHECK(loaded.settings.introVideos == IntroVideos::Skip);
        CHECK(loaded.warnings.size() == 1);
        CHECK(loaded.warnings[0].find(version) != std::string::npos);
    }
}
