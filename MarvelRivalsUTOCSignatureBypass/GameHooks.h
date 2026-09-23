#pragma once
#include <filesystem>

#include "ByteSearch.h"

namespace bypass
{
    // Lets unsigned mod containers load.
    [[nodiscard]] bool InstallSigningKeysBypass(const CodeRegion& text);

    // Keeps cosmetic mods in Paks/~mods mounted when the game tries to unmount them after login.
    [[nodiscard]] bool InstallUnmountGuard(const CodeRegion& text, const CodeRegion& rdata, const std::filesystem::path& gameDirectory);
}
