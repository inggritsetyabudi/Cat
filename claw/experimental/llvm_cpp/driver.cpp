#include "adapter.h"

#include "analysis/ownership.h"
#include "analysis/sema.h"
#include "ast/ast.h"
#include "diagnostics/diagnostics.h"
#include "ir/lir.h"
#include "ir/oir.h"
#include "workspace/workspace.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void usage() {
    std::cerr << "Experimental, object-only LLVM C++ API PoC (not the production backend).\n"
              << "Usage: claw-llvm-poc <single-file.cat> -o <output.o> [--emit-ir <output.ll>]\n"
              << "       [--runtime-i32] [--arithmetic-i32] [--strings] [--singleton-i32] [--pair-i32]\n"
              << "       [--local-binding-keys] (locals only, not universal identity)\n"
              << "Link separately with the prebuilt runtime and LLD.\n";
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage();
        return 0;
    }
    if (argc < 4) {
        usage();
        return 1;
    }
    claw::experimental::llvm_cpp::ObjectOptions options;
    bool haveOutput = false;
    bool haveIr = false;
    bool localBindingKeys = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view flag(argv[i]);
        if (flag == "-o" && i + 1 < argc && !haveOutput) {
            options.objectPath = argv[++i];
            haveOutput = true;
        } else if (flag == "--emit-ir" && i + 1 < argc && !haveIr) {
            options.irPath = argv[++i];
            haveIr = true;
        } else if (flag == "--runtime-i32" && !options.enableRuntimeI32) {
            options.enableRuntimeI32 = true;
        } else if (flag == "--arithmetic-i32" && !options.enableArithmeticI32) {
            options.enableArithmeticI32 = true;
        } else if (flag == "--strings" && !options.enableStrings) {
            options.enableStrings = true;
        } else if (flag == "--singleton-i32" && !options.enableSingletonI32) {
            options.enableSingletonI32 = true;
        } else if (flag == "--pair-i32" && !options.enablePairI32) {
            options.enablePairI32 = true;
        } else if (flag == "--local-binding-keys" && !localBindingKeys) {
            localBindingKeys = true;
        } else {
            usage();
            return 1;
        }
    }
    if (!haveOutput || options.objectPath.empty() || (haveIr && options.irPath.empty())) {
        usage();
        return 1;
    }
    claw::workspace::LoadedProject project;
    try {
        const std::filesystem::path input(argv[1]);
        if (input.extension() != ".cat" || !std::filesystem::is_regular_file(input)) {
            throw std::runtime_error("PoC accepts only a regular single .cat source file; workspace/config inputs are unsupported.");
        }
        // Prevent accidentally replacing source or aliasing object and IR artifacts.
        const auto normalized = [](const std::filesystem::path& path) {
            return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
        };
        const auto sourcePath = normalized(input);
        const auto objectPath = normalized(options.objectPath);
        const auto aliases = [](const std::filesystem::path& left, const std::filesystem::path& right) {
            if (left == right) return true;
            std::error_code error;
            return std::filesystem::equivalent(left, right, error) && !error;
        };
        if (aliases(sourcePath, objectPath) ||
            (haveIr && (aliases(normalized(options.irPath), sourcePath) ||
                        aliases(normalized(options.irPath), objectPath)))) {
            throw std::runtime_error("Input, object, and IR paths must be distinct.");
        }
        claw::workspace::ProjectLoader loader;
        project = loader.load(input);
        if (project.structuredPackage || project.config.has_value() || project.units.size() != 1 ||
            !project.units.front().importedBindings.empty()) {
            throw std::runtime_error("PoC supports standalone single-module files only; imports and workspaces are unsupported.");
        }
        auto& unit = project.units[project.entryIndex];
        claw::frontend::SemanticAnalyzer sema(unit.importedBindings, project.target, localBindingKeys);
        sema.analyze(unit.ast.get());
        for (const auto& warning : sema.warnings()) {
            std::cerr << claw::frontend::formatDiagnostic(warning, unit.path.string(), unit.source) << '\n';
        }
        const claw::frontend::FnDecl* entry = nullptr;
        for (const auto& declaration : unit.ast->declarations) {
            if (const auto* fn = dynamic_cast<const claw::frontend::FnDecl*>(declaration.get());
                fn && fn->name == "main") {
                if (entry) throw std::runtime_error("Duplicate main entry declaration.");
                entry = fn;
                if (fn->isShared) throw std::runtime_error("PoC entry main cannot be share.");
            }
        }
        const auto* signature = entry ? sema.lookupFunctionSignature(entry) : nullptr;
        if (!signature || !signature->paramTypes.empty() || !signature->returnType.viewKind.empty() ||
            (signature->returnType.name != "Int32" && signature->returnType.name != "Unit")) {
            throw std::runtime_error("Entry must define fn main() returning Unit or Int32 with no parameters.");
        }
        claw::frontend::OwnershipChecker ownership;
        ownership.check(unit.ast.get(), sema);
        const std::vector<claw::frontend::OirUnitView> units{
            {unit.ast.get(), &sema, &ownership.result()}};
        const auto lir = claw::frontend::buildLirProgram(
            claw::frontend::buildOirProgram(unit.ast->name, units));
        std::string error;
        if (!claw::experimental::llvm_cpp::emitObject(lir, options, error)) {
            throw std::runtime_error(error);
        }
        std::cout << "Experimental native object: " << options.objectPath << '\n';
        return 0;
    } catch (const claw::frontend::DiagnosticError& error) {
        for (const auto& diagnostic : error.diagnostics()) {
            const std::string fallback = project.units.empty() ? std::string(argv[1]) : project.units.front().path.string();
            const std::string source = project.units.empty() ? std::string{} : project.units.front().source;
            std::cerr << claw::frontend::formatDiagnostic(diagnostic, fallback, source) << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "error[llvm-cpp-poc]: " << error.what() << '\n';
    }
    return 1;
}
