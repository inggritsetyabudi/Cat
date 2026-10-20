#include "codegen/native_object.h"
#include "codegen/llvm.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FileUtilities.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>

#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#if LLVM_VERSION_MAJOR != 23 || LLVM_VERSION_MINOR != 1 || LLVM_VERSION_PATCH != 3
#error "Cat native object migration requires LLVM 23.1.3"
#endif
#if !defined(__linux__) || !defined(__x86_64__)
#error "Cat native object migration is currently gated to Linux x86_64"
#endif

namespace claw::codegen {
namespace {

void verify(const llvm::Module& module) {
    std::string message;
    llvm::raw_string_ostream diagnostics(message);
    if (llvm::verifyModule(module, &diagnostics)) {
        throw std::runtime_error("Cat LLVM object verification failed: " + message);
    }
}

void publishObject(const std::filesystem::path& output, llvm::ArrayRef<char> bytes) {
    // A failed parser, verifier, optimizer or code generator must not truncate
    // a previously emitted object. The temporary file is on the same filesystem.
    int descriptor = -1;
    llvm::SmallString<256> temporary;
    if (const auto error = llvm::sys::fs::createUniqueFile(
            output.string() + ".tmp-%%%%%%", descriptor, temporary)) {
        throw std::runtime_error("Cannot stage Cat native object: " + error.message());
    }
    llvm::FileRemover cleanup(temporary);
    llvm::raw_fd_ostream stream(descriptor, true);
    stream.write(bytes.data(), bytes.size());
    stream.close();
    if (stream.has_error()) {
        const auto error = stream.error();
        stream.clear_error();
        throw std::runtime_error("Cannot write Cat native object: " + error.message());
    }
    if (const auto error = llvm::sys::fs::rename(temporary, output.string())) {
        throw std::runtime_error("Cannot publish Cat native object: " + error.message());
    }
    cleanup.releaseFile();
}

} // namespace

std::string emitNativeObject(std::string_view nativeIr, const std::filesystem::path& output, bool optimize) {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        if (llvm::InitializeNativeTarget() || llvm::InitializeNativeTargetAsmPrinter()) {
            throw std::runtime_error("Cannot initialize Cat native LLVM target.");
        }
    });

    llvm::LLVMContext context;
    llvm::SMDiagnostic diagnostic;
    auto module = llvm::parseAssemblyString(
        llvm::StringRef(nativeIr.data(), nativeIr.size()), diagnostic, context);
    if (!module) {
        std::string message;
        llvm::raw_string_ostream stream(message);
        diagnostic.print("Cat LLVM object", stream);
        throw std::runtime_error("Cannot parse Cat native LLVM IR: " + message);
    }

    const llvm::Triple expected{std::string(nativeTargetTriple())};
    if (module->getTargetTriple() != expected || expected.getArch() != llvm::Triple::x86_64 ||
        !expected.isOSLinux() || !expected.isOSBinFormatELF()) {
        throw std::runtime_error("Cat LLVM object target must match native Linux x86_64 ELF.");
    }
    std::string targetError;
    const auto* target = llvm::TargetRegistry::lookupTarget(expected, targetError);
    if (!target) throw std::runtime_error("Cannot resolve Cat LLVM target: " + targetError);
    llvm::TargetOptions options;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
        expected, "generic", "", options, llvm::Reloc::PIC_, std::nullopt,
        optimize ? llvm::CodeGenOptLevel::Default : llvm::CodeGenOptLevel::None));
    if (!machine) throw std::runtime_error("Cannot create Cat LLVM TargetMachine.");
    const auto layout = machine->createDataLayout();
    const bool hadLayout = !module->getDataLayoutStr().empty();
    if (!layout.isLittleEndian() || layout.getPointerSizeInBits() != 64 ||
        (hadLayout && module->getDataLayout() != layout)) {
        throw std::runtime_error("Cat LLVM object DataLayout is incompatible with its native target.");
    }
    module->setDataLayout(layout);
    verify(*module);
    // Keep Cat's inspectable pre-optimization IR spelling stable. LLVM's
    // printer can rewrite constants (for example i8 255 to i8 -1); that would
    // break the existing artifact contract despite identical machine bits.
    // The parsed module has already verified this exact text under this layout.
    std::string verifiedIr(nativeIr);
    if (!hadLayout) {
        verifiedIr = "target datalayout = \"" + layout.getStringRepresentation() + "\"\n" + verifiedIr;
    }

    if (optimize) {
        llvm::LoopAnalysisManager loops;
        llvm::FunctionAnalysisManager functions;
        llvm::CGSCCAnalysisManager callGraph;
        llvm::ModuleAnalysisManager modules;
        llvm::PassBuilder builder(machine.get());
        builder.registerModuleAnalyses(modules);
        builder.registerCGSCCAnalyses(callGraph);
        builder.registerFunctionAnalyses(functions);
        builder.registerLoopAnalyses(loops);
        builder.crossRegisterProxies(loops, functions, callGraph, modules);
        auto pipeline = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
        pipeline.run(*module, modules);
        verify(*module);
    }

    llvm::SmallVector<char, 0> bytes;
    llvm::raw_svector_ostream stream(bytes);
    llvm::legacy::PassManager passes;
    if (machine->addPassesToEmitFile(passes, stream, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        throw std::runtime_error("Cat LLVM target cannot emit native objects.");
    }
    passes.run(*module);
    if (bytes.empty()) throw std::runtime_error("Cat LLVM emitted an empty native object.");
    publishObject(output, bytes);
    return verifiedIr;
}

} // namespace claw::codegen
