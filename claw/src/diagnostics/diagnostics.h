#pragma once

#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace claw::frontend {

struct SourceSpan {
    size_t line = 0;
    size_t column = 0;
    size_t length = 1;

    bool isValid() const;
};

struct DiagnosticLabel {
    SourceSpan span;
    std::string label;
    std::string path;
};

enum class DiagnosticSeverity {
    Error,
    Warning,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string stage;
    std::string message;
    SourceSpan span;
    std::string path;
    std::string primaryLabel;
    std::vector<DiagnosticLabel> secondaryLabels;

    Diagnostic() = default;
    Diagnostic(
        std::string stage,
        std::string message,
        SourceSpan span = {},
        std::string path = {},
        DiagnosticSeverity severity = DiagnosticSeverity::Error,
        std::string primaryLabel = {},
        std::vector<DiagnosticLabel> secondaryLabels = {})
        : severity(severity),
          stage(std::move(stage)),
          message(std::move(message)),
          span(span),
          path(std::move(path)),
          primaryLabel(std::move(primaryLabel)),
          secondaryLabels(std::move(secondaryLabels)) {}
};

class DiagnosticError final : public std::exception {
public:
    DiagnosticError(std::string summary, std::vector<Diagnostic> diagnostics);

    const char* what() const noexcept override;
    const std::vector<Diagnostic>& diagnostics() const;

private:
    std::string summary_;
    std::vector<Diagnostic> diagnostics_;
};

std::string formatDiagnostic(
    const Diagnostic& diagnostic,
    std::string_view path,
    std::string_view source);
std::string formatBuildDiagnostic(
    const Diagnostic& diagnostic,
    std::string_view path,
    std::string_view source);
std::string formatDiagnostics(
    const std::vector<Diagnostic>& diagnostics,
    std::string_view path,
    std::string_view source);

} // namespace claw::frontend