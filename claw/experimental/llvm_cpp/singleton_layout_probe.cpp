#include "analysis/ownership.h"
#include "analysis/sema.h"
#include "ast/ast.h"
#include "ir/lir.h"
#include "workspace/workspace.h"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>

#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

using namespace claw::frontend;
namespace {
void need(bool ok, const std::string& msg) { if (!ok) throw std::runtime_error(msg); }
std::string json(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '\\' || c == '\"') out += '\\'; if (c == '\n') out += "\\n"; else out += c; }
    return out + "\"";
}
std::string kind(TypeLayoutKind k) { return describeTypeLayoutKind(k); }
std::string pass(AbiPassKind k) { return describeAbiPassKind(k); }
struct Result { std::string source, triple, effective, native; TypeLayoutInfo layout; size_t clangSize, clangAlign, clangOffset, nativeSize, nativeAlign, nativeOffset; };
Result inspect(const std::string& source, const std::string& triple, const std::string& effective) {
    claw::workspace::ProjectLoader loader;
    auto project = loader.load(source);
    auto& unit = project.units.at(project.entryIndex);
    SemanticAnalyzer sema(unit.importedBindings, project.target);
    sema.analyze(unit.ast.get());
    OwnershipChecker own; own.check(unit.ast.get(), sema);
    auto oir = buildOirProgram(unit.ast->name, {{unit.ast.get(), &sema, &own.result()}});
    auto lir = buildLirProgram(oir);

    const auto* makeSig = sema.lookupFunctionSignature("make");
    const auto* takeSig = sema.lookupFunctionSignature("take");
    need(makeSig && takeSig, "fixture must define make and take");
    auto layout = computeTypeLayout(makeSig->returnType, sema.result(), sema.targetSpec());
    need(layout.has_value(), "no TypeLayoutInfo for make return Tuple[Int32]");
    need(layout->kind == TypeLayoutKind::Aggregate && layout->repr == "claw-internal" && layout->abi == "claw" && !layout->ffiStable, "unexpected tuple metadata");
    need(layout->typeName == "Tuple[Int32]" && !layout->isTemplate, "not the concrete singleton candidate");
    need(layout->fields.size() == 1 && layout->fields[0].name == "0" && layout->fields[0].type.describe() == "Int32", "tuple canonical field 0 is not Int32");
    need(layout->passKind == AbiPassKind::Direct, "tuple pass kind is not Direct");
    need(takeSig->paramTypes.size() == 1 && takeSig->paramTypes[0].describe() == makeSig->returnType.describe(), "take parameter does not match tuple");

    unsigned makeCount = 0, takeCount = 0, mainCount = 0;
    for (const auto& realm : lir.realms) for (const auto& decl : realm.decls) if (auto* f = std::get_if<LirFunction>(&decl)) {
        if (f->name == "make") {
            ++makeCount;
            bool alloc = false, store = false, ret = false;
            int storeIndex = -1, returnIndex = -1, index = 0;
            for (const auto& b : f->blocks) for (const auto& i : b.insts) {
                if (const auto* object = std::get_if<LirObjectInst>(&i)) alloc |= object->type == layout->typeName;
                if (const auto* field = std::get_if<LirStoreFieldInst>(&i)) {
                    store |= field->field == "0" && field->value.type == "Int32";
                    if (storeIndex < 0 && field->field == "0") storeIndex = index;
                }
                if (const auto* retInst = std::get_if<LirReturnInst>(&i)) {
                    ret |= retInst->value.type == layout->typeName;
                    if (returnIndex < 0) returnIndex = index;
                }
                ++index;
            }
            // Canonical production LIR must expose storage, field initialization, then return.
            need(alloc && store && ret && storeIndex < returnIndex, "make lacks canonical alloc/store_field-before-return LIR");
        } else if (f->name == "take") {
            ++takeCount;
            bool field = false; for (const auto& b : f->blocks) for (const auto& i : b.insts) if (const auto* projection = std::get_if<LirFieldInst>(&i)) field |= projection->field == "0" && projection->type == "Int32";
            need(field, "take lacks canonical field 0 LIR");
        } else if (f->name == "main") {
            ++mainCount;
            bool wholeStore = false, call = false;
            for (const auto& b : f->blocks) for (const auto& i : b.insts) {
                if (const auto* store = std::get_if<LirStoreInst>(&i)) wholeStore |= store->value.type == layout->typeName;
                if (const auto* callInst = std::get_if<LirCallInst>(&i)) call |= callInst->callee == "take" && callInst->type == "Int32" && callInst->args.size() == 1 && callInst->args[0].type == layout->typeName;
            }
            need(wholeStore && call, "main lacks canonical whole-value store/call LIR");
        }
    }
    need(makeCount == 1 && takeCount == 1 && mainCount == 1, "canonical fixture functions missing or duplicated");

    static const bool initialized = !llvm::InitializeNativeTarget() && !llvm::InitializeNativeTargetAsmPrinter();
    need(initialized, "LLVM native target initialization failed");
    llvm::Triple tt(triple);
#if defined(_WIN32)
    need(tt.getArch() == llvm::Triple::x86_64 && tt.isWindowsGNUEnvironment() && tt.isOSBinFormatCOFF(), "only native Windows MinGW UCRT COFF is supported");
#else
    need(tt.getArch() == llvm::Triple::x86_64 && tt.isOSLinux() && tt.isOSBinFormatELF(), "only native Linux x86_64 ELF is supported");
#endif
    std::string err; const auto* target = llvm::TargetRegistry::lookupTarget(tt, err); need(target != nullptr, "cannot resolve target: " + err);
    llvm::TargetOptions opts; std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(tt, "generic", "", opts, llvm::Reloc::PIC_)); need(tm != nullptr, "cannot create TargetMachine");
    llvm::DataLayout clangLayout(effective), nativeLayout = tm->createDataLayout();
    need(clangLayout.isLittleEndian() && nativeLayout.isLittleEndian() && clangLayout.getPointerSizeInBits() == 64 && nativeLayout.getPointerSizeInBits() == 64, "native little-endian 64-bit DataLayout required");
    llvm::LLVMContext ctx; auto* tuple = llvm::StructType::get(ctx, {llvm::Type::getInt32Ty(ctx)}, false);
    auto metrics = [&](const llvm::DataLayout& dl) { return std::array<size_t,3>{dl.getTypeAllocSize(tuple).getFixedValue(), dl.getABITypeAlign(tuple).value(), dl.getStructLayout(tuple)->getElementOffset(0)}; };
    const auto c = metrics(clangLayout), n = metrics(nativeLayout);
    need(c == n, "supplied effective Clang DataLayout differs from native TargetMachine DataLayout");
    need(c[0] == layout->sizeBytes && c[1] == layout->alignBytes && c[2] == layout->fields[0].offsetBytes, "LLVM layout differs from Cat metadata");
    need(clangLayout.getTypeStoreSize(tuple).getFixedValue() == c[0] && nativeLayout.getTypeStoreSize(tuple).getFixedValue() == n[0], "unexpected singleton store padding");
    need(c == std::array<size_t,3>{4, 4, 0}, "inspected layout differs from expected singleton size4/align4/offset0");
    return {source, triple, effective, nativeLayout.getStringRepresentation(), *layout, c[0], c[1], c[2], n[0], n[1], n[2]};
}
}
int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "usage: singleton_layout_probe SOURCE TARGET_TRIPLE EFFECTIVE_DATALAYOUT\n"; return 2; }
    try {
        auto r = inspect(argv[1], argv[2], argv[3]);
        const auto& f = r.layout.fields[0];
        std::cout << "{\"source\":" << json(r.source) << ",\"target_triple\":" << json(r.triple) << ",\"effective_data_layout\":" << json(r.effective)
            << ",\"native_target_machine_data_layout\":" << json(r.native)
            << ",\"clang\":{\"size\":" << r.clangSize << ",\"abi_align\":" << r.clangAlign << ",\"field0_offset\":" << r.clangOffset << "},\"native_target_machine\":{\"size\":" << r.nativeSize << ",\"abi_align\":" << r.nativeAlign << ",\"field0_offset\":" << r.nativeOffset << "},\"metadata\":{\"type\":" << json(r.layout.typeName) << ",\"kind\":" << json(kind(r.layout.kind)) << ",\"repr\":" << json(r.layout.repr) << ",\"abi\":" << json(r.layout.abi) << ",\"ffi_stable\":false,\"pass\":" << json(pass(r.layout.passKind) ) << ",\"size\":" << r.layout.sizeBytes << ",\"align\":" << r.layout.alignBytes << ",\"field0\":{\"type\":" << json(f.type.describe()) << ",\"offset\":" << f.offsetBytes << ",\"size\":" << f.sizeBytes << ",\"align\":" << f.alignBytes << "}},\"validated\":{\"make_alloc_store_field_before_return\":true,\"take_field0\":true,\"main_whole_store_and_call\":true,\"malformed_lir_acceptance\":false}}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "singleton layout probe: " << e.what() << '\n'; return 1; }
}
