#include "GameHooks.h"

#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>

#include "IntroSkip.h"
#include "Locators.h"
#include "Log.h"
#include "ModPolicy.h"
#include "ModRemount.h"

namespace bypass
{
    namespace
    {
        // The engine's delegate layout (FDelegateBase): it reads delegateSize at +8, then allocation; zero means unbound, so nothing is verified.
        struct PakSigningKeys
        {
            void* allocation;
            std::int32_t delegateSize;
        };

        using SigningKeysFn = PakSigningKeys* (*)();
        using UnmountFn = bool (*)(void* pakPlatformFile, const wchar_t* pakFilename);
        // Five arguments: both of the game's callers set exactly these (rcx, rdx, r8d, r9, [rsp+20h]) on the 2026-10-09 build.
        using MountFn = bool (*)(void* pakPlatformFile, const wchar_t* pakFilename, std::uint32_t order, const wchar_t* mountPoint, bool loadIndex);

        SigningKeysFn originalSigningKeys = nullptr;
        UnmountFn originalUnmount = nullptr;
        MountFn originalMount = nullptr;
        PakSigningKeys noSigningKeys{};

        // Live for the whole process: the engine can call the hooks until the moment it exits.
        ModPolicy* modPolicy = nullptr;
        MountLog* mountLog = nullptr;
        ModsAfterLogin modsAfterLogin = ModsAfterLogin::Remount;

        decltype(&CreateFileW) originalCreateFileW = nullptr;
        decltype(&CreateFile2) originalCreateFile2 = nullptr;
        decltype(&GetFileAttributesW) originalGetFileAttributesW = nullptr;
        decltype(&GetFileAttributesExW) originalGetFileAttributesExW = nullptr;
        SeenPaths skippedIntroVideos;

        struct Detoured
        {
            void** original;
            void* replacement;
        };

        // One transaction, so either every hook is attached or none is.
        bool Detour(std::initializer_list<Detoured> hooks)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            for (const Detoured& hook : hooks) DetourAttach(hook.original, hook.replacement);
            return DetourTransactionCommit() == NO_ERROR;
        }

        bool Detour(void** original, void* replacement)
        {
            return Detour({ { original, replacement } });
        }

        PakSigningKeys* HookedSigningKeys()
        {
            noSigningKeys = {};
            return &noSigningKeys;
        }

        // The policy only throws on allocation failure; fall back to the folder rule, which cannot.
        UnmountVerdict DecideSafely(std::wstring_view pak) noexcept
        {
            try
            {
                return modPolicy->Decide(pak);
            }
            catch (const std::exception& error)
            {
                log::Error(error.what());
                return IsInModsFolder(pak) ? UnmountVerdict::KeepUnchecked : UnmountVerdict::AllowNotAMod;
            }
        }

        // Runs on the engine's call frame, so formatting failures must stay in here.
        void LogUnmount(std::string_view outcome, UnmountVerdict verdict, std::wstring_view pak) noexcept
        {
            try
            {
                log::Info(std::format("{} ({}): {}", outcome, Describe(verdict), log::Narrow(pak)));
            }
            catch (const std::exception&)
            {
                log::Error("Could not format a log line");
            }
        }

        bool HookedMount(void* pakPlatformFile, const wchar_t* pakFilename, std::uint32_t order, const wchar_t* mountPoint, bool loadIndex)
        {
            return MountAndRecord(*mountLog, pakFilename, order, mountPoint, loadIndex,
                [&] { return originalMount(pakPlatformFile, pakFilename, order, mountPoint, loadIndex); },
                [](const std::exception& error)
                {
                    log::Error("A mod's mount was not recorded, so it won't be remounted after login:");
                    log::Error(error.what());
                });
        }

        // Repeats the engine's first mount of the pak; the platform file is the one that just unmounted it.
        void Remount(void* pakPlatformFile, std::wstring_view pak, UnmountVerdict verdict) noexcept
        {
            std::optional<MountCall> call;
            try
            {
                call = mountLog->TakeForRemount(pak, std::chrono::steady_clock::now());
            }
            catch (const std::exception& error)
            {
                log::Error("Left a mod unmounted after an error:");
                log::Error(error.what());
                return;
            }
            if (!call)
            {
                LogUnmount("Left unmounted (first mount not seen, or remounted moments ago)", verdict, pak);
                return;
            }

            const bool remounted = originalMount(pakPlatformFile, call->pakFilename.c_str(), call->order, call->MountPointOrNull(), call->loadIndex);
            // Mount reports false for a pak with no .utoc even though the pak is mounted. KeepUnchecked also covers an
            // unreadable .utoc, where false is a real failure, so the line says what was returned rather than guessing.
            const char* outcome = remounted ? "Remounted"
                : verdict == UnmountVerdict::KeepUnchecked ? "Remount returned false (expected for a pak without .utoc)"
                : "Remount failed";
            LogUnmount(outcome, verdict, pak);
        }

        bool HookedUnmount(void* pakPlatformFile, const wchar_t* pakFilename)
        {
            const std::wstring_view pak = pakFilename != nullptr ? pakFilename : L"";
            const UnmountVerdict verdict = DecideSafely(pak);
            if (!KeepsMounted(verdict))
            {
                const bool unmounted = originalUnmount(pakPlatformFile, pakFilename);
                if (verdict == UnmountVerdict::AllowGameplayMod) LogUnmount(unmounted ? "Unmounted" : "Unmount failed", verdict, pak);
                return unmounted;
            }

            if (modsAfterLogin == ModsAfterLogin::Keep) LogUnmount("Kept mounted", verdict, pak);
            return HandleCosmeticUnmount(modsAfterLogin,
                [&]
                {
                    const bool unmounted = originalUnmount(pakPlatformFile, pakFilename);
                    if (!unmounted) LogUnmount("Unmount failed", verdict, pak);
                    return unmounted;
                },
                [&] { Remount(pakPlatformFile, pak, verdict); });
        }

        void LogSkippedIntroVideo(std::wstring_view path) noexcept
        {
            try
            {
                log::Info(std::format("Skipped intro video: {}", log::Narrow(path)));
            }
            catch (const std::exception&)
            {
                log::Error("Could not format a log line");
            }
        }

        // Runs on every file open and attribute query in the process, so a miss costs one scan and nothing more.
        // The log writes through an already open handle with WriteFile, which is not hooked, so this cannot recurse.
        bool HidesIntroVideo(const wchar_t* path) noexcept
        {
            if (path == nullptr || !IsIntroVideo(path)) return false;
            if (skippedIntroVideos.Insert(path)) LogSkippedIntroVideo(path);
            SetLastError(ERROR_FILE_NOT_FOUND);
            return true;
        }

        HANDLE WINAPI HookedCreateFileW(LPCWSTR fileName, DWORD access, DWORD shareMode, LPSECURITY_ATTRIBUTES security,
            DWORD disposition, DWORD flags, HANDLE templateFile)
        {
            if (HidesIntroVideo(fileName)) return INVALID_HANDLE_VALUE;
            return originalCreateFileW(fileName, access, shareMode, security, disposition, flags, templateFile);
        }

        HANDLE WINAPI HookedCreateFile2(LPCWSTR fileName, DWORD access, DWORD shareMode, DWORD disposition,
            LPCREATEFILE2_EXTENDED_PARAMETERS parameters)
        {
            if (HidesIntroVideo(fileName)) return INVALID_HANDLE_VALUE;
            return originalCreateFile2(fileName, access, shareMode, disposition, parameters);
        }

        DWORD WINAPI HookedGetFileAttributesW(LPCWSTR fileName)
        {
            if (HidesIntroVideo(fileName)) return INVALID_FILE_ATTRIBUTES;
            return originalGetFileAttributesW(fileName);
        }

        BOOL WINAPI HookedGetFileAttributesExW(LPCWSTR fileName, GET_FILEEX_INFO_LEVELS level, LPVOID information)
        {
            if (HidesIntroVideo(fileName)) return FALSE;
            return originalGetFileAttributesExW(fileName, level, information);
        }

        template <typename Fn>
        bool Resolve(HMODULE module, const char* name, Fn& function)
        {
            function = reinterpret_cast<Fn>(GetProcAddress(module, name));
            return function != nullptr;
        }

        std::string Address(const CodeRegion& text, std::uintptr_t address)
        {
            return std::format(".text+{:#x}", address - text.base);
        }
    }

    bool InstallSigningKeysBypass(const CodeRegion& text)
    {
        const Located target = LocateSigningKeysDelegate(text);
        if (!target.Found())
        {
            log::Error(std::format("Signature bypass not installed: {}", Describe(target.error)));
            return false;
        }

        originalSigningKeys = reinterpret_cast<SigningKeysFn>(target.address);
        if (!Detour(reinterpret_cast<void**>(&originalSigningKeys), reinterpret_cast<void*>(&HookedSigningKeys)))
        {
            log::Error("Signature bypass not installed: hook failed");
            return false;
        }
        log::Info(std::format("Signature bypass installed at {}", Address(text, target.address)));
        return true;
    }

    bool InstallModsAfterLogin(ModsAfterLogin mode, const CodeRegion& text, const CodeRegion& rdata, const std::filesystem::path& gameDirectory)
    {
        const Located unmount = LocatePakUnmount(text, rdata);
        if (!unmount.Found())
        {
            const char* hint = unmount.error == LocateError::AlreadyHooked ? " (remove MarvelRivalsUnmountBlocker.asi)" : "";
            log::Error(std::format("Mods will unload at login: unmount {}{}", Describe(unmount.error), hint));
            return false;
        }

        const bool remount = mode == ModsAfterLogin::Remount;
        const Located mount = remount ? LocatePakMount(text, rdata) : Located{};
        if (remount && !mount.Found())
        {
            log::Error(std::format("Mods will unload at login: mount {}", Describe(mount.error)));
            return false;
        }

        // The mount log and Mount hook exist only for Remount; Keep never reaches the code that uses them.
        auto policy = std::make_unique<ModPolicy>(gameDirectory);
        auto mounts = remount ? std::make_unique<MountLog>() : nullptr;
        modPolicy = policy.get();
        mountLog = mounts.get();
        modsAfterLogin = mode;
        originalUnmount = reinterpret_cast<UnmountFn>(unmount.address);
        if (remount) originalMount = reinterpret_cast<MountFn>(mount.address);
        const Detoured unmountHook{ reinterpret_cast<void**>(&originalUnmount), reinterpret_cast<void*>(&HookedUnmount) };
        const Detoured mountHook{ reinterpret_cast<void**>(&originalMount), reinterpret_cast<void*>(&HookedMount) };
        if (!(remount ? Detour({ unmountHook, mountHook }) : Detour({ unmountHook })))
        {
            modPolicy = nullptr;
            mountLog = nullptr;
            log::Error("Mods will unload at login: hook failed");
            return false;
        }
        policy.release();
        mounts.release();

        if (remount)
            log::Info(std::format("Mod remount installed (unmount at {}, mount at {})", Address(text, unmount.address), Address(text, mount.address)));
        else
            log::Info(std::format("Unmount guard installed at {}", Address(text, unmount.address)));
        return true;
    }

    bool InstallIntroSkip()
    {
        // KernelBase holds the implementations: kernel32's exports and the api-ms-win-core-file sets both land here.
        const HMODULE kernelBase = GetModuleHandleW(L"KernelBase.dll");
        if (kernelBase == nullptr
            || !Resolve(kernelBase, "CreateFileW", originalCreateFileW)
            || !Resolve(kernelBase, "CreateFile2", originalCreateFile2)
            || !Resolve(kernelBase, "GetFileAttributesW", originalGetFileAttributesW)
            || !Resolve(kernelBase, "GetFileAttributesExW", originalGetFileAttributesExW))
        {
            log::Error("Intro skip not installed: KernelBase file functions not found");
            return false;
        }

        if (!Detour({
                { reinterpret_cast<void**>(&originalCreateFileW), reinterpret_cast<void*>(&HookedCreateFileW) },
                { reinterpret_cast<void**>(&originalCreateFile2), reinterpret_cast<void*>(&HookedCreateFile2) },
                { reinterpret_cast<void**>(&originalGetFileAttributesW), reinterpret_cast<void*>(&HookedGetFileAttributesW) },
                { reinterpret_cast<void**>(&originalGetFileAttributesExW), reinterpret_cast<void*>(&HookedGetFileAttributesExW) },
            }))
        {
            log::Error("Intro skip not installed: hook failed");
            return false;
        }
        log::Info("Intro skip installed");
        return true;
    }
}
