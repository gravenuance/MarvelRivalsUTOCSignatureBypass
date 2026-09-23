#include "Log.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace bypass::log
{
    namespace
    {
        std::mutex writeLock;
        HANDLE file = INVALID_HANDLE_VALUE;

        std::string_view LevelName(Level level) noexcept
        {
            switch (level)
            {
            case Level::Info: return "INFO ";
            case Level::Warning: return "WARN ";
            case Level::Error: return "ERROR";
            }
            return "?    ";
        }
    }

    void Open(const std::filesystem::path& path) noexcept
    {
        try
        {
            std::filesystem::path previous = path;
            previous += L".1";
            MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        catch (const std::bad_alloc&)
        {
            // Losing the previous run's log is acceptable; failing to start is not.
        }

        std::scoped_lock guard(writeLock);
        file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    void Write(Level level, std::string_view message) noexcept
    {
        SYSTEMTIME now{};
        GetSystemTime(&now);
        // One write per line: these run on the game thread during the login unmount burst.
        char line[1024];
        const int prefixLength = std::snprintf(line, sizeof line, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ %s ",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, LevelName(level).data());
        const std::size_t room = sizeof line - static_cast<std::size_t>(prefixLength) - 2;
        const std::size_t kept = message.size() < room ? message.size() : room;
        std::memcpy(line + prefixLength, message.data(), kept);
        std::memcpy(line + prefixLength + kept, "\r\n", 2);

        std::scoped_lock guard(writeLock);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(static_cast<std::size_t>(prefixLength) + kept + 2), &written, nullptr);
    }

    std::string Narrow(std::wstring_view text)
    {
        if (text.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string narrow(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), length, nullptr, nullptr);
        return narrow;
    }
}
