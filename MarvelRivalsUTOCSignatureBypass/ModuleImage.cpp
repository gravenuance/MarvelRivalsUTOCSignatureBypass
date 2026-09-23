#include "ModuleImage.h"

#include <cstring>

namespace bypass
{
    std::optional<CodeRegion> FindSection(HMODULE module, std::string_view name) noexcept
    {
        if (module == nullptr || name.size() > IMAGE_SIZEOF_SHORT_NAME) return std::nullopt;

        const auto* image = reinterpret_cast<const std::uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return std::nullopt;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return std::nullopt;

        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            char sectionName[IMAGE_SIZEOF_SHORT_NAME + 1] = {};
            std::memcpy(sectionName, section->Name, IMAGE_SIZEOF_SHORT_NAME);
            if (name != sectionName) continue;

            const std::uint8_t* begin = image + section->VirtualAddress;
            return CodeRegion{ Bytes(begin, section->Misc.VirtualSize), reinterpret_cast<std::uintptr_t>(begin) };
        }
        return std::nullopt;
    }
}
