#include "diagnostics/diagnostics.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace claw::frontend;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "diagnostic format regression: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    Diagnostic diagnostic("ownership", "cannot move `value` while borrowed", {2, 2, 500},
        "source with spaces.cat", DiagnosticSeverity::Error, "move occurs here",
        {{{1, 2, 3}, "borrow starts here", {}}, {{4, 3, 2}, "other source", "other.cat"}});
    const std::string source = "\tref value\r\n\tvalue\r\n";
    const auto compact = formatBuildDiagnostic(diagnostic, "fallback.cat", source);
    require(compact.starts_with("error: cannot move"), "compact severity header");
    require(compact.find("error[ownership]") == std::string::npos, "no compact stage noise");
    require(compact.find("source with spaces.cat:2:2") != std::string::npos, "primary location");
    require(compact.find("\t^^^^^ move occurs here") != std::string::npos, "tab-aligned clipped primary");
    require(compact.find("\t^^^ borrow starts here") != std::string::npos, "real secondary span");
    require(compact.find("note: other.cat:4:3 other source") != std::string::npos, "cross-file location note");
    require(compact.find('\r') == std::string::npos, "CRLF source trimming");
    const auto legacy = formatDiagnostic(diagnostic, "fallback.cat", source);
    require(legacy.starts_with("error[ownership]:"), "legacy stage retained");
    require(legacy.find(std::string(500, '^')) != std::string::npos, "legacy caret behavior unchanged");
    require(legacy.find("move occurs here") == std::string::npos, "legacy formatting unchanged");
    Diagnostic unknown("parse", "unknown location");
    require(formatBuildDiagnostic(unknown, "missing.cat", {}) ==
        "error: unknown location\n --> missing.cat", "unknown location printed exactly once");
    Diagnostic unavailable("parse", "source unavailable", {1, 1, 1});
    require(formatBuildDiagnostic(unavailable, "missing.cat", {}).find('^') == std::string::npos,
        "unavailable source must not get invented caret");
    std::cout << "[diagnostic-format/pass] compact spans, tabs, labels and legacy contract\n";
}
