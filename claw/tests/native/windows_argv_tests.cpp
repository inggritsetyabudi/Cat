#include "driver/windows_argv.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#endif

int main(int argc, char** argv) {
    const std::vector<std::string> roundTrip = {
        "", "plain", "two words", "a\tb", "C:\\directory with spaces\\file.ll",
        "C:\\trailing slash\\", "two trailing slashes\\\\", "embedded\"quote",
        "slash\\\"quote", "two slashes\\\\\"quote", "& literal; $ value", "\"",
    };
    if (argc > 1 && std::string(argv[1]) == "--child") {
        if (argc != static_cast<int>(roundTrip.size()) + 2) {
            std::cerr << "Windows CRT changed the argument count: " << argc << '\n';
            return 1;
        }
        for (std::size_t i = 0; i < roundTrip.size(); ++i) {
            if (argv[i + 2] != roundTrip[i]) {
                std::cerr << "Windows CRT changed argument " << i << '\n';
                return 1;
            }
        }
        return 0;
    }
    const std::vector<std::pair<std::wstring, std::wstring>> cases = {
        {L"", L"\"\""},
        {L"clang", L"\"clang\""},
        {L"two words", L"\"two words\""},
        {L"a\tb", L"\"a\tb\""},
        {LR"(C:\directory with spaces\file.ll)", LR"("C:\directory with spaces\file.ll")"},
        {L"C:\\trailing slash\\", L"\"C:\\trailing slash\\\\\""},
        {L"two slashes\\\\", L"\"two slashes\\\\\\\\\""},
        {L"embedded\"quote", L"\"embedded\\\"quote\""},
        {L"slash\\\"quote", L"\"slash\\\\\\\"quote\""},
        {L"two slashes\\\\\"quote", L"\"two slashes\\\\\\\\\\\"quote\""},
        {L"& literal; $ value", L"\"& literal; $ value\""},
        {L"\"", L"\"\\\"\""},
    };
    for (std::size_t i = 0; i < cases.size(); ++i) {
        if (claw::driver::quoteWindowsCommandLineArgument(cases[i].first) != cases[i].second) {
            std::cerr << "Windows serialization case " << i << " failed\n";
            return 1;
        }
    }
    std::cout << "[native-argv/pass] " << cases.size() << " exact serialization cases\n";
#if defined(_WIN32)
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        std::cerr << "Cannot resolve the Windows CRT probe executable\n";
        return 1;
    }
    executable.resize(length);
    std::vector<std::wstring> storage = {
        claw::driver::quoteWindowsCommandLineArgument(executable),
        claw::driver::quoteWindowsCommandLineArgument(L"--child"),
    };
    for (const auto& value : roundTrip) {
        storage.push_back(claw::driver::quoteWindowsCommandLineArgument(
            std::wstring(value.begin(), value.end())));
    }
    std::vector<const wchar_t*> childArgv;
    for (const auto& value : storage) {
        childArgv.push_back(value.c_str());
    }
    childArgv.push_back(nullptr);
    if (_wspawnvp(_P_WAIT, executable.c_str(), childArgv.data()) != 0) {
        std::cerr << "Windows _wspawnvp round-trip failed\n";
        return 1;
    }
    std::cout << "[native-argv/pass] Windows CRT _wspawnvp exact argument round-trip\n";
#else
    std::cout << "[native-argv/skip] CRT process round-trip is Windows-only; serialization checked\n";
#endif
    return 0;
}
