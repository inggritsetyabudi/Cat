#include "experimental/llvm_cpp/pair_admission.h"

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace claw::experimental::llvm_cpp {
namespace {
using namespace claw::frontend;
constexpr const char* pairType = "Tuple[Int32, Int32]";

[[noreturn]] void reject(const std::string& why) {
    throw std::runtime_error("LLVM C++ PoC unsupported pair admission: " + why);
}
bool identifier(const std::string& s) {
    if (s.empty() || !(s[0] == '_' || (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))) return false;
    for (size_t i = 1; i < s.size(); ++i)
        if (!(s[i] == '_' || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9'))) return false;
    return true;
}
bool allowed(const std::string& t) { return t == "Int32" || t == "Bool" || t == "Unit" || t == pairType; }
bool emptyProv(const Provenance& p) { return p.isValid() && !p.source && !p.syntheticStorageId; }
std::string key(const LirValue& v) { return v.bindingKey.empty() ? v.text : v.bindingKey; }
std::string key(const LirParam& p) { return p.bindingKey.empty() ? p.name : p.bindingKey; }
std::string key(const LirObjectInst& o) { return o.bindingKey.empty() ? o.name : o.bindingKey; }
void valueShape(const LirValue& v) {
    if (!allowed(v.type)) reject("malformed operand type");
    if (v.type == "Unit") {
        if (!v.isUnit || v.text != "unit" || !v.bindingKey.empty() || !emptyProv(v.provenance))
            reject("malformed Unit operand");
        return;
    }
    if (v.isUnit || !emptyProv(v.provenance)) reject("malformed non-Unit operand");
}
struct FunctionSet { std::unordered_map<std::string,const LirFunction*> name, symbol; };

void checkAbi(const LirFunction& f, const std::string& realm) {
    if (!identifier(f.name) || !allowed(f.returnType)) reject("invalid function name or return type");
    if (f.linkage.abi != "claw" || f.linkage.linkage != LinkageKind::Internal || f.linkage.ffiStable || f.linkage.symbol != realm + "::" + f.name) reject("non-canonical function linkage");
    const auto expected = f.returnType == pairType ? AbiPassKind::Direct : f.returnType == "Unit" ? AbiPassKind::Void : AbiPassKind::Scalar;
    if (f.returnPassKind != expected) reject("return ABI");
    std::unordered_set<std::string> names, ids;
    for (const auto& p : f.params) {
        if (!identifier(p.name) || !names.insert(p.name).second || key(p).empty() || !ids.insert(key(p)).second || !allowed(p.type) || p.type == "Unit") reject("malformed parameter");
        if (p.passKind != (p.type == pairType ? AbiPassKind::Direct : AbiPassKind::Scalar)) reject("parameter ABI");
    }
}

bool canonicalBuiltin(const LirChoice& c, const std::string& realm) {
    if (!c.layout || !c.layout->isTemplate || c.layout->abi != "claw" || c.layout->ffiStable ||
        c.linkage.abi != "claw" || c.linkage.linkage != LinkageKind::Internal || c.linkage.ffiStable || c.linkage.symbol != realm + "::" + c.name || c.cases.size() != 2) return false;
    if (c.name == "Result" && c.typeParams == std::vector<std::string>{"T", "E"})
        return c.cases[0].name == "Ok" && c.cases[0].payloadTypes == std::vector<std::string>{"T"} && c.cases[1].name == "Fail" && c.cases[1].payloadTypes == std::vector<std::string>{"E"};
    if (c.name == "Maybe" && c.typeParams == std::vector<std::string>{"T"})
        return c.cases[0].name == "None" && c.cases[0].payloadTypes.empty() && c.cases[1].name == "Some" && c.cases[1].payloadTypes == std::vector<std::string>{"T"};
    return false;
}

void checkFunction(const LirFunction& f, const FunctionSet& fs, const std::string& entry, bool arithmetic) {
    if (f.blocks.empty()) reject("function without blocks");
    std::unordered_set<std::string> labels;
    for (size_t i = 0; i < f.blocks.size(); ++i) {
        const auto& b = f.blocks[i];
        if (!identifier(b.label) || !labels.insert(b.label).second || b.rawRegion || b.insts.empty()) reject("malformed linear block");
        if ((i == 0 && (!b.predecessors.empty() || b.label != "entry")) || (i && (b.predecessors.size() != 1 || b.predecessors[0] != f.blocks[i-1].label))) reject("non-linear predecessor chain");
        if (i + 1 < f.blocks.size()) { auto g = std::get_if<LirGotoInst>(&b.insts.back()); if (!g || g->targetLabel != f.blocks[i+1].label) reject("missing linear goto"); }
        else if (!std::holds_alternative<LirReturnInst>(b.insts.back())) reject("last instruction is not return");
    }
    struct Binding { std::string type; unsigned bits = 0; bool stack = false; };
    std::unordered_map<std::string, Binding> bindings;
    for (const auto& p : f.params) { const auto id = key(p); if (!bindings.emplace(id, Binding{p.type, p.type == pairType ? 3u : 1u, false}).second) reject("duplicate parameter binding"); }
    auto read = [&](const LirValue& v, bool whole = true) {
        valueShape(v); const auto id = key(v); auto it = bindings.find(id);
        if (it != bindings.end()) {
            if (it->second.type != v.type || (v.type == pairType && whole && it->second.bits != 3) || it->second.bits == 0) reject("uninitialized or mismatched binding read");
            return;
        }
        if (v.bindingKey.empty() && v.type == "Bool" && (v.text == "true" || v.text == "false")) return;
        if (v.bindingKey.empty() && v.type == "Int32") { int32_t n; auto r = std::from_chars(v.text.data(), v.text.data()+v.text.size(), n); if (r.ec == std::errc{} && r.ptr == v.text.data()+v.text.size()) return; }
        reject("unresolved operand");
    };
    auto define = [&](const std::string& id, const std::string& type) { if (id.empty() || !allowed(type) || type == "Unit" || bindings.contains(id)) reject("duplicate or invalid SSA definition"); bindings.emplace(id, Binding{type, type == pairType ? 3u : 1u, false}); };
    for (const auto& b : f.blocks) {
        for (size_t n = 0; n + 1 < b.insts.size(); ++n) {
            std::visit([&](const auto& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T,LirObjectInst>) {
                    const auto id = key(x); if (id.empty() || x.name.empty() || x.isMutable || !allowed(x.type) || x.type == "Unit" || x.storage != "stack" || bindings.contains(id)) reject("malformed immutable stack binding"); bindings.emplace(id, Binding{x.type,0,true});
                } else if constexpr (std::is_same_v<T,LirStoreInst>) {
                    const auto id = x.targetBindingKey.empty() ? x.target : x.targetBindingKey; auto it = bindings.find(id); if (it == bindings.end() || !it->second.stack || it->second.bits != 0) reject("invalid or duplicate stack store"); read(x.value); if (it->second.type != x.value.type) reject("stack store type mismatch"); it->second.bits = it->second.type == pairType ? 3u : 1u;
                } else if constexpr (std::is_same_v<T,LirStoreFieldInst>) {
                    if (x.safety != LirSafetyTag::ProvenSafe || (x.field != "0" && x.field != "1") || x.object.type != pairType || x.value.type != "Int32" || x.object.isUnit || x.value.isUnit || !emptyProv(x.object.provenance) || !emptyProv(x.value.provenance)) reject("malformed pair field store"); const auto id = key(x.object); auto it = bindings.find(id); if (it == bindings.end() || !it->second.stack || it->second.type != pairType || (it->second.bits & (1u << (x.field == "1"))) != 0) reject("invalid or duplicate pair field store"); read(x.value); it->second.bits |= 1u << (x.field == "1");
                } else if constexpr (std::is_same_v<T,LirFieldInst>) {
                    if (x.safety != LirSafetyTag::ProvenSafe || (x.field != "0" && x.field != "1") || x.type != "Int32" || x.object.type != pairType || x.object.isUnit || !emptyProv(x.provenance) || !emptyProv(x.object.provenance)) reject("malformed pair projection"); read(x.object); define(x.result, "Int32");
                } else if constexpr (std::is_same_v<T,LirCallInst>) {
                    if (x.kind != LirCallKind::Direct || x.safety != LirSafetyTag::None || !x.hook.empty() || x.builtinTag || x.builtinReceiver || x.externalInfo) reject("non-direct or metadata-bearing call"); const LirFunction* callee = nullptr; if (auto i=fs.name.find(x.callee); i!=fs.name.end()) callee=i->second; else if (auto i=fs.symbol.find(x.callee); i!=fs.symbol.end()) callee=i->second; if (!callee || callee->linkage.symbol == entry || callee == &f) reject("unknown, entry, or recursive call"); if (x.args.size()!=callee->params.size() || x.type!=callee->returnType) reject("call signature mismatch"); for(size_t i=0;i<x.args.size();++i){read(x.args[i]); if(x.args[i].type!=callee->params[i].type) reject("call argument mismatch");} if(x.type=="Unit"){if(x.result) reject("Unit call result");} else {if(!x.result || x.result->empty()) reject("missing call result"); define(*x.result,x.type);}
                } else if constexpr (std::is_same_v<T,LirBinaryInst>) {
                    if (!arithmetic || x.op != "add" || x.type != "Int32" || x.left.type != "Int32" || x.right.type != "Int32") reject("unsupported arithmetic"); valueShape(x.left); valueShape(x.right); read(x.left); read(x.right); define(x.result,"Int32");
                } else if constexpr (std::is_same_v<T,LirGotoInst>) { reject("goto before block terminator");
                } else { reject("unsupported instruction in pair admission"); }
            }, b.insts[n]);
        }
        const auto& term = b.insts.back();
        if (std::holds_alternative<LirGotoInst>(term)) continue;
        const auto& r = std::get<LirReturnInst>(term); valueShape(r.value);
        if (f.returnType == "Unit") { if (r.value.type != "Unit" || !r.value.isUnit || r.value.text != "unit" || !r.value.bindingKey.empty() || !emptyProv(r.value.provenance)) reject("invalid Unit return"); }
        else { if (r.value.type != f.returnType || r.value.isUnit) reject("return type mismatch"); read(r.value); }
    }
    for (const auto& [_, binding] : bindings)
        if (binding.stack && binding.type == pairType && binding.bits != 3) reject("partially initialized pair local");
}
} // namespace

void validatePairAdmission(const claw::frontend::LirProgram& program, bool enableArithmeticI32) {
    if (program.realms.size() != 1) reject("multiple realms/modules");
    const auto& realm = program.realms.front();
    if (!identifier(realm.name) || program.entryRealm != realm.name || program.entrySymbol != realm.name + "::main") reject("malformed entry realm/symbol");
    FunctionSet fs; const LirFunction* main = nullptr;
    for (const auto& d : realm.decls) {
        if (const auto* c = std::get_if<LirChoice>(&d)) { if (!canonicalBuiltin(*c, realm.name)) reject("non-canonical choice declaration"); continue; }
        const auto* f = std::get_if<LirFunction>(&d); if (!f) reject("unsupported declaration"); checkAbi(*f, realm.name);
        if (!fs.name.emplace(f->name,f).second || !fs.symbol.emplace(f->linkage.symbol,f).second) reject("duplicate function or symbol"); if (f->name == "main") main=f;
    }
    if (!main || !main->params.empty() || (main->returnType != "Int32" && main->returnType != "Unit")) reject("invalid entry main signature");
    for (const auto& [_,f] : fs.name) checkFunction(*f,fs,realm.name+"::main",enableArithmeticI32);
}
} // namespace claw::experimental::llvm_cpp
