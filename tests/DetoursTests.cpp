#include <cstdint>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>

#include "Test.h"

namespace
{
    // Starts with the same instructions as the game's FPakPlatformFile::Unmount, then returns a + b.
    constexpr std::uint8_t UnmountShapedCode[] = {
        0x48, 0x8B, 0xC4,        // mov rax, rsp
        0x48, 0x89, 0x50, 0x10,  // mov [rax+10h], rdx
        0x48, 0x8D, 0x04, 0x11,  // lea rax, [rcx+rdx]
        0xC3,                    // ret
    };

    using AddFn = std::int64_t (*)(std::int64_t, std::int64_t);
    AddFn original = nullptr;

    std::int64_t Hooked(std::int64_t a, std::int64_t b)
    {
        return 1000 + original(a, b);
    }

    LONG Apply(bool attach)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (attach) DetourAttach(reinterpret_cast<void**>(&original), reinterpret_cast<void*>(&Hooked));
        else DetourDetach(reinterpret_cast<void**>(&original), reinterpret_cast<void*>(&Hooked));
        return DetourTransactionCommit();
    }
}

TEST(DetoursHooksTheUnmountPrologueAndCallsThrough)
{
    void* code = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(code != nullptr);
    std::memcpy(code, UnmountShapedCode, sizeof UnmountShapedCode);
    FlushInstructionCache(GetCurrentProcess(), code, sizeof UnmountShapedCode);
    const auto target = reinterpret_cast<AddFn>(code);
    CHECK(target(2, 3) == 5);

    original = target;
    CHECK(Apply(true) == NO_ERROR);
    CHECK(target(2, 3) == 1005);

    CHECK(Apply(false) == NO_ERROR);
    CHECK(target(2, 3) == 5);
    VirtualFree(code, 0, MEM_RELEASE);
}
