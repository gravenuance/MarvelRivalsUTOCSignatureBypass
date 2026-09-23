#include <filesystem>
#include <format>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "GameHooks.h"
#include "Log.h"
#include "ModuleImage.h"

namespace
{
    std::filesystem::path ModulePath(HMODULE module)
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
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

    // Runs inside DllMain on purpose: the engine checks pak signatures during startup, before any later hook point.
    void Start(HMODULE self)
    {
        using namespace bypass;

        LARGE_INTEGER start{};
        QueryPerformanceCounter(&start);

        std::filesystem::path logPath = ModulePath(self);
        logPath.replace_extension(L".log");
        log::Open(logPath);

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
        log::Info(std::format("Startup finished in {:.0f} ms (signature bypass {}, unmount guard {})",
            ElapsedMilliseconds(start), signatureBypass ? "on" : "off", unmountGuard ? "on" : "off"));
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
