#pragma once
#include <filesystem>

#include "ByteSearch.h"
#include "Settings.h"

namespace bypass
{
    // Lets unsigned mod containers load.
    [[nodiscard]] bool InstallSigningKeysBypass(const CodeRegion& text);

    // Brings cosmetic mods in Paks/~mods through the game's login unmount: Remount mounts each one again right after,
    // Keep refuses the unmount. Must run before the engine mounts the mods, so Remount sees how it mounted them.
    // Installs nothing and returns false when a hook target can't be found; the game's unmount then goes ahead.
    [[nodiscard]] bool InstallModsAfterLogin(ModsAfterLogin mode, const CodeRegion& text, const CodeRegion& rdata,
        const std::filesystem::path& gameDirectory);

    // Makes the startup logo videos look deleted to every file open and attribute query in the process.
    [[nodiscard]] bool InstallIntroSkip();
}
