#pragma once
#include <filesystem>
#include <optional>

#include "ByteSearch.h"
#include "Settings.h"

namespace bypass
{
    // Lets unsigned mod containers load, by whichever of the two builds' forms the game's code matches. data is where
    // the binder form's count must live, since that hook writes through it; without it only the getter form can be
    // installed, so a build that renamed that section still gets the other hooks.
    [[nodiscard]] bool InstallSigningKeysBypass(const CodeRegion& text, const CodeRegion& rdata,
        const std::optional<CodeRegion>& data);

    // Brings cosmetic mods in Paks/~mods through the game's login unmount: Remount mounts each one again right after,
    // Keep refuses the unmount. Must run before the engine mounts the mods, so Remount sees how it mounted them.
    // Installs nothing and returns false when a hook target can't be found; the game's unmount then goes ahead.
    [[nodiscard]] bool InstallModsAfterLogin(ModsAfterLogin mode, const CodeRegion& text, const CodeRegion& rdata,
        const std::filesystem::path& gameDirectory);

    // Makes the startup logo videos look deleted to every file open and attribute query in the process.
    [[nodiscard]] bool InstallIntroSkip();
}
