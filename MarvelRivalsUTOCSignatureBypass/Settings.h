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

    struct Settings
    {
        IntroVideos introVideos = IntroVideos::Skip;
    };

    struct LoadedSettings
    {
        Settings settings;
        std::vector<std::string> warnings;
    };

    // Reads the optional ini; a missing file or key keeps the default, and a bad value is reported and replaced by it.
    [[nodiscard]] LoadedSettings LoadSettings(const std::filesystem::path& iniPath);
}
