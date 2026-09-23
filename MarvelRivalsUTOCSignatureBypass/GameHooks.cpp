#include "GameHooks.h"

#include <format>
#include <memory>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours/detours.h"

#include "Locators.h"
#include "Log.h"
#include "ModPolicy.h"

namespace bypass
{
    namespace
    {
        // Layout the engine expects back from the signing-keys delegate; zero keys means "nothing to verify".
        struct PakSigningKeys
        {
            std::uint64_t function;
            std::int32_t size;
        };

        using SigningKeysFn = PakSigningKeys* (*)();
        using UnmountFn = bool (*)(void* pakPlatformFile, const wchar_t* pakFilename);

        SigningKeysFn originalSigningKeys = nullptr;
        UnmountFn originalUnmount = nullptr;
        PakSigningKeys noSigningKeys{};

        // Lives for the whole process: the engine can call the hook until the moment it exits.
        ModPolicy* modPolicy = nullptr;

        bool Detour(void** original, void* replacement)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(original, replacement);
            return DetourTransactionCommit() == NO_ERROR;
        }

        PakSigningKeys* HookedSigningKeys()
        {
            noSigningKeys = {};
            return &noSigningKeys;
        }

        bool HookedUnmount(void* pakPlatformFile, const wchar_t* pakFilename)
        {
            const std::wstring_view name = pakFilename != nullptr ? pakFilename : L"";
            UnmountVerdict verdict = UnmountVerdict::AllowNotAMod;
            try
            {
                verdict = modPolicy->Decide(name);
            }
            catch (const std::exception& error)
            {
                log::Error(std::format("Could not check {}: {}", log::Narrow(name), error.what()));
            }

            if (verdict == UnmountVerdict::KeepCosmeticMod)
            {
                log::Info(std::format("Kept mounted ({}): {}", Describe(verdict), log::Narrow(name)));
                return false;
            }

            const bool unmounted = originalUnmount(pakPlatformFile, pakFilename);
            if (verdict != UnmountVerdict::AllowNotAMod)
                log::Info(std::format("Unmount allowed ({}): {} -> {}", Describe(verdict), log::Narrow(name), unmounted ? "unmounted" : "failed"));
            return unmounted;
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

    bool InstallUnmountGuard(const CodeRegion& text, const CodeRegion& rdata, const std::filesystem::path& gameDirectory)
    {
        const Located target = LocatePakUnmount(text, rdata);
        if (!target.Found())
        {
            const char* hint = target.error == LocateError::AlreadyHooked ? " (remove MarvelRivalsUnmountBlocker.asi)" : "";
            log::Error(std::format("Unmount guard not installed: {}{}", Describe(target.error), hint));
            return false;
        }

        modPolicy = new ModPolicy(gameDirectory);
        originalUnmount = reinterpret_cast<UnmountFn>(target.address);
        if (!Detour(reinterpret_cast<void**>(&originalUnmount), reinterpret_cast<void*>(&HookedUnmount)))
        {
            log::Error("Unmount guard not installed: hook failed");
            return false;
        }
        log::Info(std::format("Unmount guard installed at {}", Address(text, target.address)));
        return true;
    }
}
