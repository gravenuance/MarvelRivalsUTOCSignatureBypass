#include <cstdio>
#include <exception>
#include <string>
#include <string_view>

#include "Probe.h"
#include "Test.h"

namespace test
{
    std::vector<Case>& Registry()
    {
        static std::vector<Case> cases;
        return cases;
    }

    void Fail(const char* file, int line, const char* expression)
    {
        throw Failure{ std::string(file) + "(" + std::to_string(line) + "): CHECK(" + expression + ")" };
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 3 && std::wstring_view(argv[1]) == L"--probe") return RunProbe(argv[2]);
    if (argc != 1)
    {
        std::fprintf(stderr, "usage: Tests.exe [--probe <Marvel-Win64-Shipping.exe>]\n");
        return 2;
    }

    int failed = 0;
    for (const auto& testCase : test::Registry())
    {
        try
        {
            testCase.run();
            std::printf("  pass  %s\n", testCase.name);
        }
        catch (const test::Failure& failure)
        {
            ++failed;
            std::fprintf(stderr, "  FAIL  %s\n        %s\n", testCase.name, failure.message.c_str());
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::fprintf(stderr, "  FAIL  %s\n        threw: %s\n", testCase.name, error.what());
        }
    }
    std::printf("%zu tests, %d failed\n", test::Registry().size(), failed);
    return failed == 0 ? 0 : 1;
}
