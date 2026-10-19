#pragma once
#include <string>
#include <string_view>

namespace claw::driver {
// _wspawnvp joins argv with spaces without quoting it. Encode one argument for
// the child's CRT parser, not for a shell. argv[0] uses a separate CRT rule;
// the driver leaves its fixed L"clang" program name unchanged.
inline std::wstring quoteWindowsCommandLineArgument(std::wstring_view argument) {
    std::wstring quoted(1, L'"');
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            // 2N backslashes preserve N backslashes, plus one escapes the quote.
            quoted.append(backslashes * 2 + 1, L'\\');
        } else {
            quoted.append(backslashes, L'\\');
        }
        backslashes = 0;
        quoted.push_back(character);
    }
    // Protect trailing backslashes from consuming the closing quote.
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}
} // namespace claw::driver
