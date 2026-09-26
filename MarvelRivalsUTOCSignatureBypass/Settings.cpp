#include "Settings.h"

#include <format>
#include <iterator>
#include <optional>
#include <string_view>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Log.h"

namespace bypass
{
    namespace
    {
        constexpr int CurrentVersion = 1;

        // Key text as written in the file, before it is given any meaning.
        struct RawSettings
        {
            std::wstring version;
            std::wstring skipIntroVideos;
        };

        std::wstring ReadRaw(const std::filesystem::path& iniPath, const wchar_t* key)
        {
            wchar_t value[64]{};
            GetPrivateProfileStringW(L"Settings", key, L"", value, static_cast<DWORD>(std::size(value)), iniPath.c_str());
            return value;
        }

        std::optional<int> ParseVersion(std::wstring_view text) noexcept
        {
            if (text.empty() || text.size() > 9) return std::nullopt;
            int version = 0;
            for (const wchar_t c : text)
            {
                if (c < L'0' || c > L'9') return std::nullopt;
                version = version * 10 + (c - L'0');
            }
            return version;
        }

        // Rewrites an older file's raw keys into the current version's, one step per version; false for a version this build does not know.
        bool MigrateToCurrent([[maybe_unused]] RawSettings& raw, int version) noexcept
        {
            return version == CurrentVersion;
        }

        IntroVideos ParseIntroVideos(std::wstring_view text, std::vector<std::string>& warnings)
        {
            if (text == L"1") return IntroVideos::Skip;
            if (text == L"0") return IntroVideos::Play;
            if (!text.empty()) warnings.push_back(std::format("SkipIntroVideos={} is not 0 or 1; using 1", log::Narrow(text)));
            return Settings{}.introVideos;
        }
    }

    LoadedSettings LoadSettings(const std::filesystem::path& iniPath)
    {
        RawSettings raw{ ReadRaw(iniPath, L"Version"), ReadRaw(iniPath, L"SkipIntroVideos") };
        LoadedSettings loaded;
        const std::optional<int> version = raw.version.empty() ? CurrentVersion : ParseVersion(raw.version);
        if (!version || !MigrateToCurrent(raw, *version))
        {
            loaded.warnings.push_back(std::format("Settings ignored: unknown Version={}", log::Narrow(raw.version)));
            return loaded;
        }

        loaded.settings.introVideos = ParseIntroVideos(raw.skipIntroVideos, loaded.warnings);
        return loaded;
    }
}
