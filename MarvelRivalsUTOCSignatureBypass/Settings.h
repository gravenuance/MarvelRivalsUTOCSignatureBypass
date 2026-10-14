#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace bypass
{
    enum class IntroVideos
    {
        Skip,
        Play,
    };

    // What happens to cosmetic mods when the game unmounts Paks/~mods during login.
    enum class ModsAfterLogin
    {
        Remount,  // Let the unmount happen, then mount each mod again the way the engine first did.
        Keep,     // Refuse the unmount.
        Unload,   // Leave the game's unmount alone.
    };

    struct Settings
    {
        IntroVideos introVideos = IntroVideos::Skip;
        ModsAfterLogin modsAfterLogin = ModsAfterLogin::Remount;
    };

    struct LoadedSettings
    {
        Settings settings;
        std::vector<std::string> warnings;
    };

    // Reads the optional ini; a missing file or key keeps the default, and a bad value is reported and replaced by it.
    [[nodiscard]] LoadedSettings LoadSettings(const std::filesystem::path& iniPath);
}
