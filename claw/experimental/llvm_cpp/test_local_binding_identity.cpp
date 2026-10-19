#include "analysis/ownership.h"
#include "analysis/sema.h"
#include "ast/ast.h"
#include "ir/lir.h"
#include "ir/oir.h"
#include "workspace/workspace.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace claw::frontend;

namespace {
[[noreturn]] void fail(const std::string& m) { throw std::runtime_error("binding identity: " + m); }
void need(bool ok, const std::string& m) { if (!ok) fail(m); }

struct Use { const IdentExpr* ident; const BindingStmt* decl; };
struct Seen { std::vector<const BindingStmt*> bindings; std::vector<Use> uses; };

// This is deliberately test-only resolution: the compiler's resolver is not replaced.
struct Lexical {
    std::vector<std::unordered_map<std::string, const BindingStmt*>> scopes{{}};
    Seen seen;
    const BindingStmt* find(const std::string& n) const {
        for (auto i = scopes.rbegin(); i != scopes.rend(); ++i) {
            auto it = i->find(n); if (it != i->end()) return it->second;
        }
        return nullptr;
    }
    void enter() { scopes.emplace_back(); }
    void leave() { scopes.pop_back(); }
    void bind(const BindingStmt* b) { scopes.back()[b->name] = b; seen.bindings.push_back(b); }
};
void walkExpr(Expr*, Lexical&);
void walkBlock(BlockStmt* b, Lexical& l) {
    if (!b) return;
    l.enter();
    for (auto& p : b->statements) {
        Stmt* s = p.get(); if (!s) continue;
        if (auto* x = dynamic_cast<BindingStmt*>(s)) { walkExpr(x->value.get(), l); l.bind(x); continue; }
        if (auto* x = dynamic_cast<AssignStmt*>(s)) { walkExpr(x->target.get(), l); walkExpr(x->value.get(), l); continue; }
        if (auto* x = dynamic_cast<ExprStmt*>(s)) { walkExpr(x->expr.get(), l); continue; }
        if (auto* x = dynamic_cast<ReturnStmt*>(s)) { walkExpr(x->value.get(), l); continue; }
        if (auto* x = dynamic_cast<IfStmt*>(s)) { walkExpr(x->condition.get(), l); walkBlock(x->thenBlock.get(), l); walkBlock(x->elseBlock.get(), l); continue; }
        if (auto* x = dynamic_cast<LoopStmt*>(s)) { walkExpr(x->condition.get(), l); walkBlock(x->body.get(), l); continue; }
        if (auto* x = dynamic_cast<ScanStmt*>(s)) { walkExpr(x->iterable.get(), l); walkBlock(x->body.get(), l); continue; }
        if (auto* x = dynamic_cast<ScopeStmt*>(s)) { walkBlock(x->body.get(), l); continue; }
        if (auto* x = dynamic_cast<TryStmt*>(s)) { walkExpr(x->expr.get(), l); walkBlock(x->failBlock.get(), l); continue; }
        if (auto* x = dynamic_cast<PickStmt*>(s)) { walkExpr(x->value.get(), l); for (auto& c : x->branches) walkBlock(c.body.get(), l); continue; }
        if (auto* x = dynamic_cast<RawStmt*>(s)) { walkBlock(x->body.get(), l); }
    }
    l.leave();
}
void walkExpr(Expr* e, Lexical& l) {
    if (!e) return;
    if (auto* x = dynamic_cast<IdentExpr*>(e)) { l.seen.uses.push_back({x, l.find(x->name)}); return; }
    if (auto* x = dynamic_cast<BinaryExpr*>(e)) { walkExpr(x->left.get(), l); walkExpr(x->right.get(), l); return; }
    if (auto* x = dynamic_cast<UnaryExpr*>(e)) { walkExpr(x->operand.get(), l); return; }
    if (auto* x = dynamic_cast<CallExpr*>(e)) { walkExpr(x->callee.get(), l); for (auto& a : x->args) walkExpr(a.get(), l); return; }
    if (auto* x = dynamic_cast<MemberExpr*>(e)) { walkExpr(x->object.get(), l); return; }
    if (auto* x = dynamic_cast<IndexExpr*>(e)) { walkExpr(x->object.get(), l); walkExpr(x->index.get(), l); return; }
    if (auto* x = dynamic_cast<BorrowExpr*>(e)) { walkExpr(x->target.get(), l); return; }
    if (auto* x = dynamic_cast<TupleExpr*>(e)) { for (auto& a : x->elements) walkExpr(a.get(), l); return; }
    if (auto* x = dynamic_cast<ArrayExpr*>(e)) { for (auto& a : x->elements) walkExpr(a.get(), l); return; }
    if (auto* x = dynamic_cast<IfExpr*>(e)) { walkExpr(x->condition.get(), l); walkBlock(x->thenBlock.get(), l); walkBlock(x->elseBlock.get(), l); return; }
    if (auto* x = dynamic_cast<PickExpr*>(e)) { walkExpr(x->value.get(), l); for (auto& c : x->branches) walkBlock(c.body.get(), l); return; }
    if (auto* x = dynamic_cast<ShapeInitExpr*>(e)) for (auto& f : x->fields) walkExpr(f.value.get(), l);
}

struct Keys { std::unordered_set<std::string> decl, use, store, drop; };
void add(std::unordered_set<std::string>& s, const std::string& k) { if (!k.empty()) s.insert(k); }
void collectOir(const OirProgram& p, Keys& k) {
    for (const auto& r : p.realms) for (const auto& d : r.decls) if (auto* f = std::get_if<OirFunction>(&d)) for (const auto& b : f->blocks) for (const auto& in : b.insts)
        std::visit([&](const auto& x) { using T=std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T,OirBindingInst>) { add(k.decl,x.bindingKey); if(x.init) add(k.use,x.init->bindingKey); }
            else if constexpr (std::is_same_v<T,OirStoreInst>) { add(k.store,x.targetBindingKey); add(k.use,x.value.bindingKey); }
            else if constexpr (std::is_same_v<T,OirDropInst>) add(k.drop,x.bindingKey);
            else if constexpr (std::is_same_v<T,OirReturnInst>||std::is_same_v<T,OirDiscardInst>) add(k.use,x.value.bindingKey);
            else if constexpr (std::is_same_v<T,OirBranchInst>) add(k.use,x.condition.bindingKey);
            else if constexpr (std::is_same_v<T,OirCallInst>) { if(x.builtinReceiver) add(k.use,x.builtinReceiver->bindingKey); for(const auto& v:x.args) add(k.use,v.bindingKey); }
            else if constexpr (std::is_same_v<T,OirFieldInst>) add(k.use,x.object.bindingKey);
            else if constexpr (std::is_same_v<T,OirIndexInst>) { add(k.use,x.object.bindingKey); add(k.use,x.index.bindingKey); }
            else if constexpr (std::is_same_v<T,OirUnaryInst>) add(k.use,x.operand.bindingKey);
            else if constexpr (std::is_same_v<T,OirBinaryInst>) { add(k.use,x.left.bindingKey); add(k.use,x.right.bindingKey); }
            else if constexpr (std::is_same_v<T,OirScanInst>) add(k.use,x.iterable.bindingKey);
            else if constexpr (std::is_same_v<T,OirRangeScanInitInst>) add(k.use,x.iterable.bindingKey);
            else if constexpr (std::is_same_v<T,OirTryInst>) add(k.use,x.value.bindingKey);
        }, in);
}
void collectLir(const LirProgram& p, Keys& k) {
    for (const auto& r:p.realms) for (const auto& d:r.decls) if(auto* f=std::get_if<LirFunction>(&d)) for(const auto& b:f->blocks) for(const auto& in:b.insts)
        std::visit([&](const auto& x) { using T=std::decay_t<decltype(x)>;
            if constexpr(std::is_same_v<T,LirObjectInst>) add(k.decl,x.bindingKey);
            else if constexpr(std::is_same_v<T,LirStoreInst>) { add(k.store,x.targetBindingKey); add(k.use,x.value.bindingKey); }
            else if constexpr(std::is_same_v<T,LirDropInst>) add(k.drop,x.bindingKey);
            else if constexpr(std::is_same_v<T,LirCallInst>) { if(x.builtinReceiver) add(k.use,x.builtinReceiver->bindingKey); for(const auto& v:x.args) add(k.use,v.bindingKey); }
            else if constexpr(std::is_same_v<T,LirReturnInst>||std::is_same_v<T,LirDiscardInst>) add(k.use,x.value.bindingKey);
            else if constexpr(std::is_same_v<T,LirBranchInst>) add(k.use,x.condition.bindingKey);
            else if constexpr(std::is_same_v<T,LirBoundsCheckInst>) { if(x.receiver) add(k.use,x.receiver->bindingKey); for(const auto& v:x.args) add(k.use,v.bindingKey); }
            else if constexpr(std::is_same_v<T,LirFieldInst>) add(k.use,x.object.bindingKey);
            else if constexpr(std::is_same_v<T,LirIndexInst>) { add(k.use,x.object.bindingKey); add(k.use,x.index.bindingKey); }
            else if constexpr(std::is_same_v<T,LirUnaryInst>) add(k.use,x.operand.bindingKey);
            else if constexpr(std::is_same_v<T,LirBinaryInst>) { add(k.use,x.left.bindingKey); add(k.use,x.right.bindingKey); }
            else if constexpr(std::is_same_v<T,LirIterNextInst>) add(k.use,x.iterable.bindingKey);
            else if constexpr(std::is_same_v<T,LirRangeScanInitInst>) add(k.use,x.iterable.bindingKey);
            else if constexpr(std::is_same_v<T,LirTryInst>) add(k.use,x.value.bindingKey);
            else if constexpr(std::is_same_v<T,LirSwitchInst>) add(k.use,x.value.bindingKey);
        }, in);
}

struct Run { Seen seen; std::vector<std::pair<std::string,std::string>> bindings; std::unordered_set<std::string> localKeys; Keys oir, lir; OwnershipResult ownership; };
Run analyze(const std::filesystem::path& source, bool canonical) {
    claw::workspace::ProjectLoader loader; auto project=loader.load(source); auto& u=project.units[project.entryIndex];
    SemanticAnalyzer sema(u.importedBindings, project.target, canonical); sema.analyze(u.ast.get());
    Lexical lexical; for(auto& d:u.ast->declarations) if(auto* f=dynamic_cast<FnDecl*>(d.get())) walkBlock(f->body.get(), lexical);
    Run out; out.seen=std::move(lexical.seen);
    for(auto* b:out.seen.bindings) out.bindings.emplace_back(b->name,b->backendBindingKey);
    for(auto* b:out.seen.bindings) { if(canonical) need(!b->backendBindingKey.empty(),"canonical local key is empty"); if(!b->backendBindingKey.empty()) out.localKeys.insert(b->backendBindingKey); }
    for(const auto& uref:out.seen.uses) {
        need(uref.ident->backendBindingKey==(uref.decl ? uref.decl->backendBindingKey : ""),"identifier key does not match lexical declaration pointer");
    }
    for(const auto& uref:out.seen.uses) if(uref.ident->name=="input") need(uref.ident->backendBindingKey.empty(),"helper formal unexpectedly received a local key");
    OwnershipChecker own; own.check(u.ast.get(),sema); out.ownership=own.result();
    std::vector<OirUnitView> units{{u.ast.get(),&sema,&out.ownership}}; auto o=buildOirProgram(u.ast->name,units); auto l=buildLirProgram(o); collectOir(o,out.oir); collectLir(l,out.lir);
    return out;
}
}

int main(int argc,char** argv) {
    if(argc!=2) { std::cerr<<"usage: test_local_binding_identity <artifacts-dir>\n"; return 1; }
    try {
        std::filesystem::create_directories(argv[1]);
        const auto source=std::filesystem::path(argv[1])/"local_binding_identity.cat";
        std::ofstream(source) << R"CAT(fn helper(input: String) {
    println(input.as_str())
}
fn main() {
    val first = String.from("first")
    val alias = first
    helper(String.from("argument"))
    if true {
        val same = String.from("then")
        println(same.as_str())
        if true {
            val same = String.from("nested")
            println(same.as_str())
        }
        if false { return }
    } else {
        val same = String.from("else")
        println(same.as_str())
    }
    var text = String.from("old")
    text = String.from("new")
    loop {
        val nested = String.from("loop")
        println(nested.as_str())
        if false { return }
        stop
    }
    println(alias.as_str())
}
)CAT";
        auto legacy=analyze(source,false), canonical=analyze(source,true), again=analyze(source,true);
        need(canonical.bindings.size()>=7,"fixture did not cover locals, sibling branches, nested shadow, mutable, early return, and loop");
        need(canonical.bindings.size()==again.bindings.size(),"fresh analysis changed binding count");
        for(size_t i=0;i<canonical.bindings.size();++i) need(canonical.bindings[i].second==again.bindings[i].second,"fresh analysis changed canonical key at "+std::to_string(i));
        need(canonical.localKeys.size()==canonical.bindings.size(),"distinct declarations share a canonical key");
        need(!legacy.localKeys.empty(),"default false unexpectedly removed existing shadowing-key behavior");
        need(legacy.bindings.front().second.empty(),"default false changed ordinary first-local behavior");
        std::string mutableKey; for(const auto& b:canonical.bindings) if(b.first=="text") mutableKey=b.second;
        need(!mutableKey.empty(),"mutable fixture declaration missing");
        for(const Keys* keys:{&canonical.oir,&canonical.lir}) {
            need(keys->decl.size()>=canonical.localKeys.size(),"stage omitted local declaration metadata");
            for(const auto& key:canonical.localKeys) need(!keys->decl.count(key)||(keys->use.count(key)||keys->drop.count(key)),"stage lost declaration/use/drop key: "+key);
            need(keys->store.count(mutableKey),"stage lost mutable target key on store");
            need(!keys->drop.empty(),"stage has no drop metadata");
            for(const auto& key:keys->drop) need(canonical.localKeys.count(key),"stage emitted unknown drop key");
        }
        bool sawExactDrop=false; for(const auto& [_, drops]:canonical.ownership.dropsBeforeStmt) for(const auto& d:drops) { if(!d.bindingKey.empty()) { need(canonical.localKeys.count(d.bindingKey),"OwnershipResult drop has wrong key"); sawExactDrop=true; } }
        for(const auto& [_, drops]:canonical.ownership.dropsAtBlockEnd) for(const auto& d:drops) { if(!d.bindingKey.empty()) { need(canonical.localKeys.count(d.bindingKey),"block-end drop has wrong key"); sawExactDrop=true; } }
        need(sawExactDrop,"OwnershipResult contained no drop actions");
        std::cout<<"PASS local binding identity: opt-in keys, legacy default, lexical shadowing, mutable store, early return, loop, OIR/LIR and exact ownership drops\n";
        return 0;
    } catch(const DiagnosticError& e) { for(const auto& d:e.diagnostics()) std::cerr<<d.message<<'\n'; return 1; }
      catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
