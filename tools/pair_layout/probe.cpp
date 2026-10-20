#include "analysis/sema.h"
#include "analysis/types.h"
#include "ast/ast.h"
#include "workspace/workspace.h"
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#if defined(_WIN32)
#include <_mingw.h>
#if !defined(__x86_64__) || !defined(__MINGW64__) || !defined(_UCRT)
#error Native x64 MinGW UCRT is required
#endif
#elif !defined(__linux__) || !defined(__x86_64__)
#error Only native Linux x86_64 and Windows x64 UCRT are supported
#endif

using namespace claw::frontend;
namespace {
void need(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
using Metrics = std::array<uint64_t, 6>;
Metrics measure(const llvm::DataLayout& dl, llvm::StructType* pair) {
    const auto* fields = dl.getStructLayout(pair);
    return {dl.getTypeAllocSize(pair).getFixedValue(),
            dl.getTypeStoreSize(pair).getFixedValue(),
            dl.getABITypeAlign(pair).value(), dl.getPrefTypeAlign(pair).value(),
            fields->getElementOffset(0), fields->getElementOffset(1)};
}
llvm::json::Object describe(const llvm::DataLayout& dl, const Metrics& m) {
    llvm::json::Object pointers;
    for (unsigned as : {0u, 270u, 271u, 272u})
        pointers[std::to_string(as)] = dl.getPointerSizeInBits(as);
    return llvm::json::Object{{"data_layout", dl.getStringRepresentation()},
            {"little_endian", dl.isLittleEndian()},
            {"pointer_bits", std::move(pointers)},
            {"alloc_size", m[0]}, {"store_size", m[1]},
            {"abi_align", m[2]}, {"preferred_align", m[3]},
            {"field0_offset", m[4]}, {"field1_offset", m[5]}};
}
}
int main(int argc, char** argv) {
    try {
        need(std::string(LLVM_VERSION_STRING) == "23.1.3", "wrong LLVM headers");
        if (argc == 2 && std::string(argv[1]) == "--sdk-version") {
            llvm::cl::PrintVersionMessage();
            llvm::outs() << "LLVM_VERSION_STRING=" << LLVM_VERSION_STRING << '\n';
            return 0;
        }
        need(argc == 4, "usage: probe SOURCE EFFECTIVE_TARGET_TRIPLE EFFECTIVE_DATALAYOUT");
        const std::string source = argv[1], triple = argv[2], effective = argv[3];
        llvm::Triple tt(triple);
#if defined(_WIN32)
        need(tt.getArch() == llvm::Triple::x86_64 && tt.isWindowsGNUEnvironment()
             && tt.isOSBinFormatCOFF(), "not native Windows GNU COFF");
#else
        need(tt.getArch() == llvm::Triple::x86_64 && tt.isOSLinux()
             && tt.isOSBinFormatELF(), "not native Linux ELF");
#endif
        claw::workspace::ProjectLoader loader;
        auto project = loader.load(source);
        auto& unit = project.units.at(project.entryIndex);
        SemanticAnalyzer sema(unit.importedBindings, project.target);
        sema.analyze(unit.ast.get());
        const auto* make = sema.lookupFunctionSignature("make");
        const auto* take = sema.lookupFunctionSignature("take");
        need(make && take, "fixture must define make and take");
        auto layout = computeTypeLayout(make->returnType, sema.result(), sema.targetSpec());
        need(layout.has_value(), "missing Cat metadata");
        need(layout->typeName == "Tuple[Int32, Int32]" && !layout->isTemplate
             && layout->kind == TypeLayoutKind::Aggregate && layout->repr == "claw-internal"
             && layout->abi == "claw" && !layout->ffiStable
             && layout->passKind == AbiPassKind::Direct, "not exact Direct internal pair");
        need(layout->fields.size() == 2, "pair arity mismatch");
        for (unsigned i = 0; i < 2; ++i)
            need(layout->fields[i].name == std::to_string(i)
                 && layout->fields[i].type.describe() == "Int32", "pair field mismatch");
        need(take->paramTypes.size() == 1
             && take->paramTypes[0].describe() == make->returnType.describe(), "take type mismatch");
        const auto& catTarget = sema.targetSpec();
        need(catTarget.pointerWidthBits == 64 && catTarget.ptrdiffWidthBits == 64,
             "Cat target is not native64");
        need(!llvm::InitializeNativeTarget() && !llvm::InitializeNativeTargetAsmPrinter(),
             "LLVM native target initialization failed");
        std::string error;
        const auto* target = llvm::TargetRegistry::lookupTarget(tt, error);
        need(target != nullptr, "LLVM target lookup failed: " + error);
        llvm::TargetOptions options;
        std::unique_ptr<llvm::TargetMachine> tm(
            target->createTargetMachine(tt, "generic", "", options, llvm::Reloc::PIC_));
        need(tm != nullptr, "TargetMachine creation failed");
        llvm::DataLayout clangLayout(effective), nativeLayout = tm->createDataLayout();
        llvm::LLVMContext context;
        auto* i32 = llvm::Type::getInt32Ty(context);
        auto* pair = llvm::StructType::get(context, {i32, i32}, false);
        need(!pair->isPacked() && pair->getNumElements() == 2, "not literal nonpacked pair");
        const auto c = measure(clangLayout, pair), n = measure(nativeLayout, pair);
        need(c == n, "effective Clang/TargetMachine pair numeric mismatch");
        need(c[0] == layout->sizeBytes && c[2] == layout->alignBytes,
             "Cat/LLVM aggregate size or ABI alignment mismatch");
        for (unsigned i = 0; i < 2; ++i) {
            const auto& field = layout->fields[i];
            need(field.offsetBytes == c[4 + i]
                 && field.sizeBytes == clangLayout.getTypeAllocSize(i32).getFixedValue()
                 && field.sizeBytes == nativeLayout.getTypeAllocSize(i32).getFixedValue()
                 && field.alignBytes == clangLayout.getABITypeAlign(i32).value()
                 && field.alignBytes == nativeLayout.getABITypeAlign(i32).value(),
                 "Cat/LLVM field numeric mismatch");
        }
        need(c[0] == 8 && c[1] == 8 && c[2] == 4 && c[4] == 0 && c[5] == 4,
             "unexpected bounded pair layout");
        need(clangLayout.isLittleEndian() && nativeLayout.isLittleEndian(), "endian mismatch");
        for (unsigned as : {0u, 270u, 271u, 272u})
            need(clangLayout.getPointerSizeInBits(as) == nativeLayout.getPointerSizeInBits(as),
                 "pointer address-space width mismatch");
        need(clangLayout.getPointerSizeInBits(0) == catTarget.pointerWidthBits,
             "Cat/default LLVM pointer model mismatch");
        llvm::json::Array fields;
        for (const auto& field : layout->fields)
            fields.push_back(llvm::json::Object{{"name", field.name}, {"type", field.type.describe()},
                {"offset", uint64_t(field.offsetBytes)}, {"size", uint64_t(field.sizeBytes)},
                {"align", uint64_t(field.alignBytes)}});
        llvm::json::Object result{
            {"validated", true}, {"llvm_headers", LLVM_VERSION_STRING},
            {"sdk_host_triple", llvm::sys::getDefaultTargetTriple()},
            {"selected_target_triple", triple}, {"target_cpu", "generic"},
            {"relocation_model", "PIC"}, {"llvm_type", "literal nonpacked {i32,i32}"},
            {"effective_clang", describe(clangLayout, c)},
            {"native_target_machine", describe(nativeLayout, n)},
            {"cat", llvm::json::Object{{"target", catTarget.name},
                {"pointer_bits", catTarget.pointerWidthBits}, {"ptrdiff_bits", catTarget.ptrdiffWidthBits},
                {"type", layout->typeName}, {"repr", layout->repr}, {"abi", layout->abi},
                {"pass", describeAbiPassKind(layout->passKind)}, {"ffi_stable", layout->ffiStable},
                {"size", uint64_t(layout->sizeBytes)}, {"align", uint64_t(layout->alignBytes)},
                {"fields", std::move(fields)}}},
            {"scope", "numeric layout only; no C ABI or adapter pair admission"}};
        llvm::outs() << llvm::json::Value(std::move(result)) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "pair layout probe: " << error.what() << '\n';
        return 1;
    }
}
