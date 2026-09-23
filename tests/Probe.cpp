#include <chrono>
#include <cstdio>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../MarvelRivalsUTOCSignatureBypass/Locators.h"
#include "../MarvelRivalsUTOCSignatureBypass/ModuleImage.h"
#include "Probe.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    double MillisecondsSince(Clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    // Upstream's original scan, kept only to measure the new one against it.
    const void* UpstreamSigScan(const char* signature, const char* mask, const std::uint8_t* memory, std::size_t size)
    {
        const std::size_t length = std::strlen(mask);
        for (std::size_t i = 0; i + length <= size; ++i)
        {
            std::size_t j = 0;
            while (j < length && (mask[j] == '?' || static_cast<std::uint8_t>(signature[j]) == memory[i + j])) ++j;
            if (j == length) return memory + i;
        }
        return nullptr;
    }

    void Report(const char* name, const bypass::Located& located, const bypass::CodeRegion& text, double milliseconds)
    {
        if (located.Found())
            std::printf("%-22s .text+%#llx  (%.1f ms)\n", name, static_cast<unsigned long long>(located.address - text.base), milliseconds);
        else
            std::printf("%-22s not found: %s  (%.1f ms)\n", name, bypass::Describe(located.error), milliseconds);
    }
}

int RunProbe(const wchar_t* executable)
{
    // Maps the sections at their virtual layout without running any of the executable's code.
    const HMODULE mapped = LoadLibraryExW(executable, nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    if (mapped == nullptr)
    {
        std::fprintf(stderr, "Cannot map %ls (error %lu)\n", executable, GetLastError());
        return 2;
    }
    const auto image = reinterpret_cast<HMODULE>(reinterpret_cast<std::uintptr_t>(mapped) & ~std::uintptr_t{ 3 });

    const auto text = bypass::FindSection(image, ".text");
    const auto rdata = bypass::FindSection(image, ".rdata");
    if (!text || !rdata)
    {
        std::fprintf(stderr, "No .text/.rdata section in %ls\n", executable);
        FreeLibrary(mapped);
        return 2;
    }

    auto start = Clock::now();
    const auto signing = bypass::LocateSigningKeysDelegate(*text);
    Report("signing keys delegate", signing, *text, MillisecondsSince(start));

    start = Clock::now();
    const auto unmount = bypass::LocatePakUnmount(*text, *rdata);
    Report("FPakPlatformFile::Unmount", unmount, *text, MillisecondsSince(start));

    start = Clock::now();
    const void* upstream = UpstreamSigScan("\xE8\x2A\x2A\x2A\x2A\x48\x8B\xF8\x39\x70\x2A\x0F\x84\x2A\x2A\x2A\x2A",
        "x????xxxxx?xx????", text->bytes.data(), text->bytes.size());
    std::printf("%-22s %s  (%.1f ms, upstream scan over .text)\n", "upstream sigScan",
        upstream != nullptr ? "found" : "not found", MillisecondsSince(start));

    FreeLibrary(mapped);
    return signing.Found() && unmount.Found() ? 0 : 1;
}
