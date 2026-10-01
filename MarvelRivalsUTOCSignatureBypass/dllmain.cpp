#include <filesystem>
#include <format>
#include <string_view>
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "GameHooks.h"
#include "Log.h"
#include "ModuleImage.h"
#include "Settings.h"

namespace
{
    std::filesystem::path ModulePath(HMODULE module)
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "GetModuleFileNameW");
            if (length < buffer.size()) return buffer.substr(0, length);
            buffer.resize(buffer.size() * 2);
        }
    }

    double ElapsedMilliseconds(const LARGE_INTEGER& start)
    {
        LARGE_INTEGER now{};
        LARGE_INTEGER frequency{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&frequency);
        return static_cast<double>(now.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
    }

    std::string_view InstallIntroSkipUnlessDisabled(const bypass::Settings& settings)
    {
        if (settings.introVideos == bypass::IntroVideos::Play) return "disabled";
        return bypass::InstallIntroSkip() ? "on" : "off";
    }

    // Runs inside DllMain on purpose: the engine checks pak signatures during startup, before any later hook point.
    void Start(HMODULE self)
    {
        using namespace bypass;

        LARGE_INTEGER start{};
        QueryPerformanceCounter(&start);

        const std::filesystem::path selfPath = ModulePath(self);
        log::Open(std::filesystem::path(selfPath).replace_extension(L".log"));
        const LoadedSettings loaded = LoadSettings(std::filesystem::path(selfPath).replace_extension(L".ini"));
        for (const std::string& warning : loaded.warnings) log::Warning(warning);

        const std::filesystem::path gamePath = ModulePath(nullptr);
        log::Info(std::format("Loaded into {}", log::Narrow(gamePath.filename().wstring())));

        const HMODULE game = GetModuleHandleW(nullptr);
        const auto text = FindSection(game, ".text");
        const auto rdata = FindSection(game, ".rdata");
        if (!text || !rdata)
        {
            log::Error("Game code sections not found; nothing installed");
            return;
        }

        const bool signatureBypass = InstallSigningKeysBypass(*text);
        const bool unmountGuard = InstallUnmountGuard(*text, *rdata, gamePath.parent_path());
        const std::string_view introSkip = InstallIntroSkipUnlessDisabled(loaded.settings);
        log::Info(std::format("Startup finished in {:.0f} ms (signature bypass {}, unmount guard {}, intro skip {})",
            ElapsedMilliseconds(start), signatureBypass ? "on" : "off", unmountGuard ? "on" : "off", introSkip));
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(module);
    try
    {
        Start(module);
    }
    catch (const std::exception& error)
    {
        bypass::log::Error(std::format("Startup failed: {}", error.what()));
    }
    return TRUE;
}
