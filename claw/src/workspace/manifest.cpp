#include "workspace/workspace.h"

#include "diagnostics/diagnostics.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <vector>

namespace claw::workspace {

using namespace claw::frontend;

namespace {

std::string trim(std::string_view text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

bool isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifierContinue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifier(std::string_view text) {
    if (text.empty() || !isIdentifierStart(text.front())) {
        return false;
    }
    for (size_t i = 1; i < text.size(); ++i) {
        if (!isIdentifierContinue(text[i])) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> splitCommaList(std::string_view text) {
    std::vector<std::string> items;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const size_t end = comma == std::string_view::npos ? text.size() : comma;
        items.push_back(trim(text.substr(start, end - start)));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return items;
}

std::string removeLineComment(std::string_view line) {
    const size_t comment = line.find("//");
    return trim(comment == std::string_view::npos ? line : line.substr(0, comment));
}

} // namespace

ProjectLoader::ModuleManifest ProjectLoader::loadManifest(const std::filesystem::path& path) {
    const auto normalized = normalizePath(path);
    const std::string key = normalized.string();
    const auto cached = manifestCache.find(key);
    if (cached != manifestCache.end()) {
        return cached->second;
    }
    ModuleManifest manifest;
    manifest.path = normalized;
    manifest.source = readFileText(normalized);
    std::vector<Diagnostic> diagnostics;
    std::istringstream lines(manifest.source);
    std::string rawLine;
    size_t lineNumber = 0;
    while (std::getline(lines, rawLine)) {
        ++lineNumber;
        const std::string line = removeLineComment(rawLine);
        if (line.empty()) {
            continue;
        }
        if (line.rfind("pub modules", 0) == 0) {
            const size_t braceOpen = line.find('{');
            const size_t braceClose = line.rfind('}');
            if (braceOpen != std::string::npos || braceClose != std::string::npos) {
                if (braceOpen == std::string::npos || braceClose == std::string::npos || braceClose < braceOpen) {
                    diagnostics.push_back(Diagnostic{"manifest", "Expected 'pub modules {a, b}'.", {lineNumber, 1, line.size()}, normalized.string()});
                    continue;
                }
                for (const auto& item : splitCommaList(std::string_view(line).substr(braceOpen + 1, braceClose - braceOpen - 1))) {
                    if (!isIdentifier(item)) {
                        diagnostics.push_back(Diagnostic{"manifest", "Invalid module name: '" + item + "'.", {lineNumber, braceOpen + 2, std::max<size_t>(1, item.size())}, normalized.string()});
                        continue;
                    }
                    manifest.publishedModules.insert(item);
                }
                continue;
            }
            const std::string single = trim(std::string_view(line).substr(std::string_view("pub modules").size()));
            if (!isIdentifier(single)) {
                diagnostics.push_back(Diagnostic{"manifest", "Expected 'pub modules name' or 'pub modules {a, b}'.", {lineNumber, 1, line.size()}, normalized.string()});
                continue;
            }
            manifest.publishedModules.insert(single);
            continue;
        }
        if (line.rfind("pub entry", 0) == 0) {
            diagnostics.push_back(Diagnostic{
                "manifest",
                "Entry is defined by root main.cat. modules.cat only supports 'pub modules { ... }'.",
                {lineNumber, 1, line.size()},
                normalized.string()});
            continue;
        }
        diagnostics.push_back(Diagnostic{"manifest", "Unknown manifest declaration.", {lineNumber, 1, line.size()}, normalized.string()});
    }
    if (!diagnostics.empty()) {
        throwDiagnostics(std::move(diagnostics));
    }
    manifestCache[key] = manifest;
    return manifest;
}

std::optional<ProjectLoader::ModuleManifest> ProjectLoader::tryLoadManifest(const std::filesystem::path& path) {
    const auto normalized = normalizePath(path);
    if (!std::filesystem::exists(normalized)) {
        return std::nullopt;
    }
    return loadManifest(normalized);
}

} // namespace claw::workspace
