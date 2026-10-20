#include "analysis/ownership.h"
#include "analysis/sema.h"
#include "ast/ast.h"
#include "ir/lir.h"
#include "ir/oir.h"
#include "workspace/workspace.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace claw::frontend;
namespace {
void need(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error("tuple provenance: " + message);
}
bool same(const Provenance& a, const Provenance& b) {
    return a.syntheticStorageId == b.syntheticStorageId && a.source.has_value() == b.source.has_value() &&
        (!a.source || (a.source->bindingKey == b.source->bindingKey && a.source->fields == b.source->fields));
}
bool empty(const Provenance& p) { return !p.source && !p.syntheticStorageId; }
struct Declaration { std::string name, key; bool tuple; };
struct Lexical {
    std::vector<std::unordered_map<std::string, std::string>> scopes;
    std::vector<Declaration> declarations;
    size_t uses = 0, wildcards = 0;
    bool canonical;
    const std::string* find(const std::string& name) const {
        for (auto i = scopes.rbegin(); i != scopes.rend(); ++i) {
            auto found = i->find(name); if (found != i->end()) return &found->second;
        }
        return nullptr;
    }
    void bind(const std::string& name, const std::string& key, bool tuple) {
        const bool shadows = find(name) != nullptr;
        need(canonical ? !key.empty() : (shadows ? !key.empty() : key.empty()), "declaration key policy changed for " + name);
        declarations.push_back({name, key, tuple}); scopes.back()[name] = key;
    }
};
void walkBlock(const BlockStmt* block, Lexical& lexical);
void walkExpr(const Expr* expr, Lexical& lexical) {
    if (!expr) return;
    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        if (const auto* key = lexical.find(ident->name)) { need(*key == ident->backendBindingKey, "resolved use differs from lexical declaration"); ++lexical.uses; }
    } else if (auto* tuple = dynamic_cast<const TupleExpr*>(expr)) {
        for (const auto& element : tuple->elements) walkExpr(element.get(), lexical);
    } else if (auto* member = dynamic_cast<const MemberExpr*>(expr)) walkExpr(member->object.get(), lexical);
    else if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        walkExpr(call->callee.get(), lexical); for (const auto& arg : call->args) walkExpr(arg.get(), lexical);
    } else if (auto* binary = dynamic_cast<const BinaryExpr*>(expr)) { walkExpr(binary->left.get(), lexical); walkExpr(binary->right.get(), lexical); }
    else if (auto* branch = dynamic_cast<const IfExpr*>(expr)) {
        walkExpr(branch->condition.get(), lexical); walkBlock(branch->thenBlock.get(), lexical); walkBlock(branch->elseBlock.get(), lexical);
    }
    else if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        walkExpr(index->object.get(), lexical); walkExpr(index->index.get(), lexical);
    }
}
void walkPattern(const PickPattern* pattern, Lexical& lexical) {
    if (pattern->kind == PickPatternKind::Wildcard) { need(pattern->backendBindingKey.empty(), "wildcard acquired a binder"); ++lexical.wildcards; }
    else if (pattern->kind == PickPatternKind::Binding) lexical.bind(pattern->name, pattern->backendBindingKey, true);
    else for (const auto& element : pattern->elements) walkPattern(element.get(), lexical);
}
void walkBlock(const BlockStmt* block, Lexical& lexical) {
    if (!block) return;
    lexical.scopes.emplace_back();
    for (const auto& holder : block->statements) {
        if (auto* binding = dynamic_cast<const BindingStmt*>(holder.get())) {
            walkExpr(binding->value.get(), lexical);
            if (binding->tuplePattern) walkPattern(binding->tuplePattern.get(), lexical);
            else lexical.bind(binding->name, binding->backendBindingKey, false);
        } else if (auto* expression = dynamic_cast<const ExprStmt*>(holder.get())) walkExpr(expression->expr.get(), lexical);
        else if (auto* branch = dynamic_cast<const IfStmt*>(holder.get())) {
            walkExpr(branch->condition.get(), lexical); walkBlock(branch->thenBlock.get(), lexical); walkBlock(branch->elseBlock.get(), lexical);
        } else if (auto* returned = dynamic_cast<const ReturnStmt*>(holder.get())) walkExpr(returned->value.get(), lexical);
    }
    lexical.scopes.pop_back();
}
struct Stage { std::vector<Declaration> declarations; OirProgram oir; LirProgram lir; std::set<std::string> ownershipDrops; };
Stage analyze(const std::filesystem::path& source, bool canonical) {
    claw::workspace::ProjectLoader loader; auto project = loader.load(source); auto& unit = project.units[project.entryIndex];
    SemanticAnalyzer sema(unit.importedBindings, project.target, canonical); sema.analyze(unit.ast.get());
    Lexical lexical{{}, {}, 0, 0, canonical};
    for (const auto& declaration : unit.ast->declarations) if (auto* fn = dynamic_cast<const FnDecl*>(declaration.get())) walkBlock(fn->body.get(), lexical);
    need(lexical.uses >= 8 && lexical.wildcards == 5, "fixture omitted uses/wildcards: " + std::to_string(lexical.uses) + "/" + std::to_string(lexical.wildcards));
    Stage result; result.declarations = lexical.declarations;
    OwnershipChecker own; own.check(unit.ast.get(), sema);
    for (const auto& [_, drops] : own.result().dropsAtBlockEnd) for (const auto& drop : drops) result.ownershipDrops.insert(drop.bindingKey);
    for (const auto& [_, drops] : own.result().dropsBeforeStmt) for (const auto& drop : drops) result.ownershipDrops.insert(drop.bindingKey);
    result.oir = buildOirProgram(unit.ast->name, {{unit.ast.get(), &sema, &own.result()}});
    result.lir = buildLirProgram(result.oir);
    return result; // No AST pointers escape their owning Project/SemanticAnalyzer.
}
struct Record { std::string name, type, key; Provenance provenance, object; };
struct Meta { std::vector<Record> fields, drops; };
Meta metadata(const OirProgram& program) {
    Meta result;
    for (const auto& realm : program.realms) for (const auto& decl : realm.decls) if (auto* fn = std::get_if<OirFunction>(&decl))
        for (const auto& block : fn->blocks) for (const auto& inst : block.insts) {
            if (auto* field = std::get_if<OirFieldInst>(&inst)) result.fields.push_back({field->result, field->type, field->object.bindingKey, field->provenance, field->object.provenance});
            else if (auto* drop = std::get_if<OirDropInst>(&inst)) result.drops.push_back({drop->name, drop->type, drop->bindingKey, drop->provenance, {}});
        }
    return result;
}
Meta metadata(const LirProgram& program) {
    Meta result;
    for (const auto& realm : program.realms) for (const auto& decl : realm.decls) if (auto* fn = std::get_if<LirFunction>(&decl))
        for (const auto& block : fn->blocks) for (const auto& inst : block.insts) {
            if (auto* field = std::get_if<LirFieldInst>(&inst)) result.fields.push_back({field->result, field->type, field->object.bindingKey, field->provenance, field->object.provenance});
            else if (auto* drop = std::get_if<LirDropInst>(&inst)) result.drops.push_back({drop->name, drop->type, drop->bindingKey, drop->provenance, {}});
        }
    return result;
}
void compare(const std::vector<Record>& a, const std::vector<Record>& b) {
    need(a.size() == b.size(), "OIR/LIR record count differs");
    for (size_t i = 0; i < a.size(); ++i) need(a[i].name == b[i].name && a[i].type == b[i].type && a[i].key == b[i].key && same(a[i].provenance, b[i].provenance) && same(a[i].object, b[i].object), "OIR/LIR lost value/field/drop metadata");
}
void verify(const Stage& stage, bool canonical) {
    const auto o = metadata(stage.oir), l = metadata(stage.lir); compare(o.fields, l.fields); compare(o.drops, l.drops);
    std::set<std::string> declared;
    std::string moved, rebound, nested;
    for (const auto& d : stage.declarations) {
        if (canonical) need(declared.insert(d.key).second, "distinct declarations share ID");
        if (d.name == "moved") moved = d.key;
        if (d.name == "rebound") rebound = d.key;
        if (d.name == "nested" && nested.empty()) nested = d.key;
    }
    std::map<uint64_t, Record> projections;
    for (const auto& field : o.fields) {
        need(field.provenance.isValid() && field.object.isValid(), "invalid emitted projection");
        if (!field.provenance.syntheticStorageId) { need(empty(field.provenance), "untracked field acquired source metadata"); continue; }
        need(projections.emplace(*field.provenance.syntheticStorageId, field).second, "projection IDs not distinct within function");
        if (field.object.source) {
            need(field.provenance.source.has_value(), "child lost known source");
            auto parentPath = field.object.source->fields;
            need(field.provenance.source->bindingKey == field.object.source->bindingKey && field.provenance.source->fields.size() == parentPath.size() + 1, "child projection did not append a path segment");
            for (size_t i = 0; i < parentPath.size(); ++i) need(parentPath[i] == field.provenance.source->fields[i], "child changed parent path");
        }
    }
    std::set<uint64_t> dropped;
    std::map<std::string, std::set<std::vector<size_t>>> paths;
    size_t unknown = 0, direct = 0;
    for (const auto& drop : o.drops) {
        need(drop.provenance.isValid(), "invalid emitted drop");
        if (empty(drop.provenance)) { ++direct; continue; }
        need(drop.key.empty(), "synthetic drop copied parent operand binding key");
        need(drop.provenance.syntheticStorageId.has_value(), "synthetic drop lacks storage ID");
        const auto id = *drop.provenance.syntheticStorageId;
        need(dropped.insert(id).second && projections.count(id), "drop lacks unique materialized projection");
        const auto& field = projections.at(id);
        need(drop.name == field.name && drop.type == field.type && same(drop.provenance, field.provenance), "drop does not match exact field result");
        if (drop.provenance.source) {
            const auto& source = *drop.provenance.source;
            need(declared.count(source.bindingKey) && stage.ownershipDrops.count(source.bindingKey), "source lacks exact declaration/ownership root");
            need(!source.fields.empty(), "field drop has empty path");
            need(paths[source.bindingKey].insert(source.fields).second, "sibling drops merged into same path");
        } else ++unknown;
    }
    need(direct >= 3 && unknown >= 3, "fixture lost ordinary or wildcard cleanup");
    if (canonical) {
        need(paths[nested] == std::set<std::vector<size_t>>{{0, 0}, {0, 1}, {1}}, "nested tuple cleanup paths incorrect");
        need(paths[rebound] == std::set<std::vector<size_t>>{{0}, {1}}, "moved value cleanup not rooted at current owner");
        need(!paths.count(moved) && !stage.ownershipDrops.count(moved), "moved-away binder retained cleanup");
    }
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        std::filesystem::create_directories(argv[1]); const auto source = std::filesystem::path(argv[1]) / "tuple_cleanup_provenance.cat";
        std::ofstream(source) << R"CAT(fn main() {
    val ordinary = Anchor.new(1)
    if true {
        val ordinary = Anchor.new(2)
        println(ordinary.get())
    }
    if false {
        val ordinary = Anchor.new(3)
        println(ordinary.get())
    }
    val (left, right) = (17, 25)
    println(left + right)
    val (nested, _) = (((Anchor.new(11), Anchor.new(22)), Anchor.new(33)), Anchor.new(34))
    println(nested.0.0.get())
    if true {
        val (nested, _) = (Anchor.new(88), 0)
        println(nested.get())
    }
    if true {
        val (same, _) = ((Anchor.new(44), Anchor.new(45)), Anchor.new(55))
        println(same.0.get())
    }
    if false {
        val (same, _) = ((Anchor.new(66), Anchor.new(67)), Anchor.new(77))
        println(same.0.get())
    }
    val (moved, _) = ((Anchor.new(91), Anchor.new(92)), 0)
    val rebound = moved
    println(rebound.1.get())
}
)CAT";
        auto canonical = analyze(source, true), legacy = analyze(source, false);
        need(canonical.declarations.size() == 11, "fixture declaration count changed");
        verify(canonical, true); verify(legacy, false);
        need(!Provenance::forRoot("", 7).source, "unknown root acquired source");
        need(!Provenance::forRoot("root", 0).isValid(), "zero storage ID accepted");
        Provenance malformed; malformed.source = SourceProjection{"", {0}}; need(!malformed.isValid(), "empty source binding accepted");
        auto nested = Provenance::forRoot("root", 1).project(2, 2).project(0, 3);
        need(nested.source->fields == std::vector<size_t>{2, 0} && nested.syntheticStorageId == 3, "projection path/storage identity conflated");
        auto unknown = Provenance::forRoot("", 4).project(1, 5); need(!unknown.source && unknown.syntheticStorageId == 5, "projection invented unknown source");
        std::cout << "PASS tuple cleanup provenance: exact declaration/use/ownership roots, default shadowing, nested paths, move/current owner, wildcard unknown, unique projections, OIR/LIR preservation\n";
        return 0;
    } catch (const DiagnosticError& e) { for (const auto& d : e.diagnostics()) std::cerr << d.message << '\n'; }
      catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
    return 1;
}
