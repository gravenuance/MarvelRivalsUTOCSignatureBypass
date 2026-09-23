#include "Log.h"

#include <cstdio>
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
        char prefix[48];
        const int prefixLength = std::snprintf(prefix, sizeof prefix, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ %s ",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, LevelName(level).data());

        std::scoped_lock guard(writeLock);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(file, prefix, static_cast<DWORD>(prefixLength), &written, nullptr);
        WriteFile(file, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
        WriteFile(file, "\r\n", 2, &written, nullptr);
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
