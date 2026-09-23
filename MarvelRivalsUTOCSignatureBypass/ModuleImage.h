#pragma once
#include <optional>
#include <string_view>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ByteSearch.h"

namespace bypass
{
    // A named section of a module already loaded in this process, e.g. ".text" or ".rdata".
    [[nodiscard]] std::optional<CodeRegion> FindSection(HMODULE module, std::string_view name) noexcept;
}
