#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace bypass::log
{
    enum class Level
    {
        Info,
        Warning,
        Error,
    };

    // Starts a fresh log at path, keeping the previous run's log as <path>.1.
    void Open(const std::filesystem::path& path) noexcept;
    void Write(Level level, std::string_view message) noexcept;

    [[nodiscard]] std::string Narrow(std::wstring_view text);

    inline void Info(std::string_view message) noexcept { Write(Level::Info, message); }
    inline void Warning(std::string_view message) noexcept { Write(Level::Warning, message); }
    inline void Error(std::string_view message) noexcept { Write(Level::Error, message); }
}
