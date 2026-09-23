#pragma once
#include <string>
#include <vector>

namespace test
{
    struct Case
    {
        const char* name;
        void (*run)();
    };

    std::vector<Case>& Registry();

    struct Register
    {
        Register(const char* name, void (*run)()) { Registry().push_back({ name, run }); }
    };

    struct Failure
    {
        std::string message;
    };

    [[noreturn]] void Fail(const char* file, int line, const char* expression);
}

#define TEST(name)                                           \
    static void name();                                      \
    static const ::test::Register name##Registration(#name, &name); \
    static void name()

#define CHECK(expression)                                                         \
    do                                                                            \
    {                                                                             \
        if (!(expression)) ::test::Fail(__FILE__, __LINE__, #expression);         \
    } while (false)
