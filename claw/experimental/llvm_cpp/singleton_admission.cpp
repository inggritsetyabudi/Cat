#include "experimental/llvm_cpp/singleton_admission.h"

#include <charconv>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace claw::experimental::llvm_cpp {
namespace {
using namespace claw::frontend;

[[noreturn]] void reject(const std::string& why) {
    throw std::runtime_error("LLVM C++ PoC unsupported singleton admission: " + why);
}

constexpr const char* tuple = "Tuple[Int32]";

bool identifier(const std::string& s) {
    if (s.empty() || !(s[0] == '_' || (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))) return false;
    for (size_t i = 1; i < s.size(); ++i)
        if (!(s[i] == '_' || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9'))) return false;
    return true;
}

bool allowedType(const std::string& type) {
    return type == "Int32" || type == "Bool" || type == "Unit" || type == tuple;
}
bool emptyProvenance(const Provenance& p) {
    return p.isValid() && !p.source.has_value() && !p.syntheticStorageId.has_value();
}
std::string key(const LirValue& v) { return v.bindingKey.empty() ? v.text : v.bindingKey; }
std::string key(const LirParam& p) { return p.bindingKey.empty() ? p.name : p.bindingKey; }
std::string key(const LirObjectInst& o) { return o.bindingKey.empty() ? o.name : o.bindingKey; }

void checkValueShape(const LirValue& v) {
    if (!allowedType(v.type) || !emptyProvenance(v.provenance) || v.isUnit)
        reject("malformed singleton operand");
}

struct FunctionSet {
    std::unordered_map<std::string, const LirFunction*> byName;
    std::unordered_map<std::string, const LirFunction*> bySymbol;
};

void checkAbi(const LirFunction& fn, const std::string& realm) {
    if (!identifier(fn.name) || !allowedType(fn.returnType)) reject("invalid function name or return type");
    if (fn.linkage.abi != "claw" || fn.linkage.linkage != LinkageKind::Internal || fn.linkage.ffiStable ||
        fn.linkage.symbol != realm + "::" + fn.name)
        reject("non-canonical internal function linkage");
    const auto expected = fn.returnType == tuple ? AbiPassKind::Direct : fn.returnType == "Unit" ? AbiPassKind::Void : AbiPassKind::Scalar;
    if (fn.returnPassKind != expected) reject("return ABI for '" + fn.name + "'");
    std::unordered_set<std::string> names, ids;
    for (const auto& p : fn.params) {
        if (!identifier(p.name) || !names.insert(p.name).second || key(p).empty() || !ids.insert(key(p)).second ||
            !allowedType(p.type) || p.type == "Unit") reject("malformed parameter in '" + fn.name + "'");
        const auto pass = p.type == tuple ? AbiPassKind::Direct : AbiPassKind::Scalar;
        if (p.passKind != pass) reject("parameter ABI in '" + fn.name + "'");
    }
}

bool builtinTemplate(const LirChoice& c, const std::string& realm) {
    if (!c.layout || !c.layout->isTemplate || c.layout->abi != "claw" || c.layout->ffiStable ||
        c.linkage.abi != "claw" || c.linkage.linkage != LinkageKind::Internal || c.linkage.ffiStable ||
        c.linkage.symbol != realm + "::" + c.name || c.cases.size() != 2) return false;
    if (c.name == "Result" && c.typeParams == std::vector<std::string>{"T", "E"})
        return c.cases[0].name == "Ok" && c.cases[0].payloadTypes == std::vector<std::string>{"T"} &&
               c.cases[1].name == "Fail" && c.cases[1].payloadTypes == std::vector<std::string>{"E"};
    if (c.name == "Maybe" && c.typeParams == std::vector<std::string>{"T"})
        return c.cases[0].name == "None" && c.cases[0].payloadTypes.empty() &&
               c.cases[1].name == "Some" && c.cases[1].payloadTypes == std::vector<std::string>{"T"};
    return false;
}

void checkFunction(const LirFunction& fn, const FunctionSet& functions, const std::string& entrySymbol) {
    if (fn.blocks.empty()) reject("function without blocks");
    std::unordered_set<std::string> labels;
    for (size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto& b = fn.blocks[i];
        if (!identifier(b.label) || !labels.insert(b.label).second || b.rawRegion || b.insts.empty()) reject("malformed linear block");
        if ((i == 0 && (!b.predecessors.empty() || b.label != "entry")) ||
            (i > 0 && (b.predecessors.size() != 1 || b.predecessors.front() != fn.blocks[i - 1].label)))
            reject("non-linear or malformed block predecessor chain");
        const auto* lastGoto = i + 1 < fn.blocks.size() ? std::get_if<LirGotoInst>(&b.insts.back()) : nullptr;
        if (i + 1 < fn.blocks.size()) {
            if (!lastGoto || lastGoto->targetLabel != fn.blocks[i + 1].label) reject("block does not end with the next linear goto");
        } else if (!std::holds_alternative<LirReturnInst>(b.insts.back())) reject("last instruction is not return");
    }

    std::unordered_map<std::string, std::string> slotTypes;
    std::unordered_set<std::string> initialized;
    std::unordered_set<std::string> params;
    std::unordered_set<std::string> stackSlots;
    for (const auto& p : fn.params) {
        slotTypes.emplace(key(p), p.type); initialized.insert(key(p)); params.insert(key(p));
    }
    auto read = [&](const LirValue& v) {
        checkValueShape(v);
        const auto id = key(v);
        if (const auto it = slotTypes.find(id); it != slotTypes.end()) {
            if (it->second != v.type || !initialized.contains(id)) reject("uninitialized or mismatched binding read in '" + fn.name + "'");
            return;
        }
        if (v.bindingKey.empty() && v.type == "Bool" && (v.text == "true" || v.text == "false")) return;
        if (v.bindingKey.empty() && v.type == "Int32") {
            int32_t n = 0;
            const auto parsed = std::from_chars(v.text.data(), v.text.data() + v.text.size(), n);
            if (parsed.ec == std::errc{} && parsed.ptr == v.text.data() + v.text.size()) return;
        }
        reject("unresolved singleton operand '" + v.text + "'");
    };
    auto define = [&](const std::string& id, const std::string& type) {
        if (id.empty() || !allowedType(type) || type == "Unit" || slotTypes.contains(id)) reject("duplicate or invalid SSA definition in '" + fn.name + "'");
        slotTypes.emplace(id, type); initialized.insert(id);
    };

    for (const auto& block : fn.blocks) {
        for (size_t index = 0; index + 1 < block.insts.size(); ++index) {
            const auto& instruction = block.insts[index];
            std::visit([&](const auto& inst) {
                using T = std::decay_t<decltype(inst)>;
                if constexpr (std::is_same_v<T, LirObjectInst>) {
                    const auto id = key(inst);
                    if (id.empty() || inst.isMutable || !allowedType(inst.type) || inst.type == "Unit" || inst.storage != "stack" || slotTypes.contains(id)) reject("malformed or colliding immutable stack local");
                    slotTypes.emplace(id, inst.type);
                    stackSlots.insert(id);
                } else if constexpr (std::is_same_v<T, LirStoreInst>) {
                    const auto id = inst.targetBindingKey.empty() ? inst.target : inst.targetBindingKey;
                    const auto found = slotTypes.find(id);
                    if (found == slotTypes.end() || !stackSlots.contains(id)) reject("store target is not a local stack slot");
                    if (initialized.contains(id)) reject("immutable local stored more than once");
                    read(inst.value);
                    if (found->second != inst.value.type) reject("store type mismatch");
                    initialized.insert(id);
                } else if constexpr (std::is_same_v<T, LirStoreFieldInst>) {
                    if (inst.safety != LirSafetyTag::ProvenSafe || inst.field != "0" || !emptyProvenance(inst.object.provenance) || !emptyProvenance(inst.value.provenance) || inst.object.isUnit || inst.value.isUnit || inst.object.type != tuple || inst.value.type != "Int32") reject("malformed tuple field construction");
                    const auto id = key(inst.object);
                    if (id.empty() || !stackSlots.contains(id) || slotTypes.at(id) != tuple || initialized.contains(id)) reject("tuple field target is not an uninitialized local");
                    read(inst.value); initialized.insert(id);
                } else if constexpr (std::is_same_v<T, LirFieldInst>) {
                    if (inst.safety != LirSafetyTag::ProvenSafe || inst.field != "0" || inst.type != "Int32" || inst.object.type != tuple || !emptyProvenance(inst.provenance) || inst.object.isUnit) reject("malformed tuple projection");
                    read(inst.object); define(inst.result, "Int32");
                } else if constexpr (std::is_same_v<T, LirCallInst>) {
                    if (inst.kind != LirCallKind::Direct || inst.safety != LirSafetyTag::None || !inst.hook.empty() || inst.builtinTag || inst.builtinReceiver || inst.externalInfo) reject("non-direct or metadata-bearing call");
                    const LirFunction* callee = nullptr;
                    if (const auto it = functions.byName.find(inst.callee); it != functions.byName.end()) callee = it->second;
                    else if (const auto it = functions.bySymbol.find(inst.callee); it != functions.bySymbol.end()) callee = it->second;
                    if (!callee) reject("unknown direct call target");
                    const auto& calleeRef = *callee;
                    if (calleeRef.linkage.symbol == entrySymbol || callee == &fn) reject("entry or recursive direct call");
                    if (inst.args.size() != calleeRef.params.size() || inst.type != calleeRef.returnType) reject("direct call signature mismatch");
                    for (size_t i = 0; i < inst.args.size(); ++i) { read(inst.args[i]); if (inst.args[i].type != calleeRef.params[i].type) reject("direct call argument mismatch"); }
                    if (inst.type == "Unit") { if (inst.result) reject("Unit call result"); }
                    else { if (!inst.result || inst.result->empty()) reject("missing call result"); define(*inst.result, inst.type); }
                } else {
                    reject("unsupported instruction in singleton admission");
                }
            }, instruction);
        }
        // Earlier blocks end in the already-validated goto to the next block.
        // Definitions and initialized stack contents dominate that entire chain.
        if (std::holds_alternative<LirGotoInst>(block.insts.back())) continue;
        const auto& ret = std::get<LirReturnInst>(block.insts.back());
        if (fn.returnType == "Unit") {
            if (ret.value.type != "Unit" || !ret.value.isUnit || ret.value.text != "unit" ||
                !ret.value.bindingKey.empty() || !emptyProvenance(ret.value.provenance)) reject("Unit return must be exact initialized Unit");
        } else {
            if (ret.value.isUnit || ret.value.type != fn.returnType) reject("return type mismatch");
            read(ret.value);
        }
    }
}

} // namespace

void validateSingletonAdmission(const claw::frontend::LirProgram& program) {
    if (program.realms.size() != 1) reject("multiple realms/modules");
    const auto& realm = program.realms.front();
    if (!identifier(realm.name) || program.entryRealm != realm.name || program.entrySymbol != realm.name + "::main") reject("malformed entry realm/symbol");
    FunctionSet functions;
    const LirFunction* main = nullptr;
    for (const auto& decl : realm.decls) {
        if (const auto* choice = std::get_if<LirChoice>(&decl)) { if (!builtinTemplate(*choice, realm.name)) reject("non-canonical choice declaration"); continue; }
        const auto* fn = std::get_if<LirFunction>(&decl);
        if (!fn) reject("non-function declaration");
        checkAbi(*fn, realm.name);
        if (!functions.byName.emplace(fn->name, fn).second || !functions.bySymbol.emplace(fn->linkage.symbol, fn).second) reject("duplicate function or symbol");
        if (fn->name == "main") main = fn;
    }
    if (!main || !main->params.empty() || (main->returnType != "Int32" && main->returnType != "Unit")) reject("entry main signature");
    for (const auto& [_, fn] : functions.byName) checkFunction(*fn, functions, realm.name + "::main");
}

} // namespace claw::experimental::llvm_cpp
