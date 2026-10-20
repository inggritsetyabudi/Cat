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
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace claw::frontend;
namespace {
void need(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("parameter identity: " + message);
}

// Independent test-only lexical resolution. Each entry points to a declaration's
// actual metadata field, not to a token recovered from IR or cleanup output.
struct Lexical {
    std::vector<std::unordered_map<std::string, const std::string*>> scopes;
    size_t resolvedUses = 0;
    std::string functionName;
    std::unordered_map<std::string, std::string>* locals = nullptr;
    void enter() { scopes.emplace_back(); }
    void leave() { scopes.pop_back(); }
    const std::string* find(const std::string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        return nullptr;
    }
};
void walkExpr(Expr*, Lexical&);
void walkBlock(BlockStmt* block, Lexical& lexical) {
    if (!block) return;
    lexical.enter();
    for (auto& statement : block->statements) {
        auto* stmt = statement.get();
        if (auto* x = dynamic_cast<BindingStmt*>(stmt)) {
            walkExpr(x->value.get(), lexical);
            lexical.scopes.back()[x->name] = &x->backendBindingKey;
            if (lexical.locals) {
                const auto label = lexical.functionName == "helper" && x->name == "input" ? "shadow" : x->name;
                (*lexical.locals)[lexical.functionName + "." + label] = x->backendBindingKey;
            }
        } else if (auto* x = dynamic_cast<ExprStmt*>(stmt)) {
            walkExpr(x->expr.get(), lexical);
        } else if (auto* x = dynamic_cast<ReturnStmt*>(stmt)) {
            walkExpr(x->value.get(), lexical);
        } else if (auto* x = dynamic_cast<IfStmt*>(stmt)) {
            walkExpr(x->condition.get(), lexical);
            walkBlock(x->thenBlock.get(), lexical);
            walkBlock(x->elseBlock.get(), lexical);
        } else if (auto* x = dynamic_cast<AssignStmt*>(stmt)) {
            walkExpr(x->target.get(), lexical);
            walkExpr(x->value.get(), lexical);
        } else {
            need(false, "unhandled statement in metadata fixture");
        }
    }
    lexical.leave();
}
void walkExpr(Expr* expr, Lexical& lexical) {
    if (!expr) return;
    if (auto* x = dynamic_cast<IdentExpr*>(expr)) {
        const auto* declaration = lexical.find(x->name);
        need(x->backendBindingKey == (declaration ? *declaration : ""),
             "resolved use mismatches lexical declaration: " + x->name);
        if (declaration) ++lexical.resolvedUses;
    } else if (auto* x = dynamic_cast<CallExpr*>(expr)) {
        walkExpr(x->callee.get(), lexical);
        for (auto& arg : x->args) walkExpr(arg.get(), lexical);
    } else if (auto* x = dynamic_cast<MemberExpr*>(expr)) {
        walkExpr(x->object.get(), lexical);
    } else if (auto* x = dynamic_cast<BorrowExpr*>(expr)) {
        walkExpr(x->target.get(), lexical);
    } else if (auto* x = dynamic_cast<BinaryExpr*>(expr)) {
        walkExpr(x->left.get(), lexical);
        walkExpr(x->right.get(), lexical);
    } else if (auto* x = dynamic_cast<IfExpr*>(expr)) {
        walkExpr(x->condition.get(), lexical);
        walkBlock(x->thenBlock.get(), lexical);
        walkBlock(x->elseBlock.get(), lexical);
    }
    // The remaining fixture expressions are literals, with no declaration use.
}

struct Stage {
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> formals;
    std::unordered_map<std::string, int> drops;
    std::unordered_set<std::string> returned;
    std::vector<std::string> helperActuals;
};
template<class Program>
Stage collect(const Program& program) {
    Stage out;
    for (const auto& realm : program.realms) for (const auto& declaration : realm.decls) {
        std::visit([&](const auto& fn) {
            if constexpr (requires { fn.params; fn.blocks; fn.returnType; }) {
                for (const auto& param : fn.params)
                    out.formals[fn.name].emplace_back(param.name, param.bindingKey);
                for (const auto& block : fn.blocks) for (const auto& instruction : block.insts) {
                    std::visit([&](const auto& inst) {
                        using T = std::decay_t<decltype(inst)>;
                        if constexpr (std::is_same_v<T, OirDropInst> || std::is_same_v<T, LirDropInst>) {
                            ++out.drops[inst.bindingKey];
                        } else if constexpr (std::is_same_v<T, OirReturnInst> || std::is_same_v<T, LirReturnInst>) {
                            out.returned.insert(inst.value.bindingKey);
                        } else if constexpr (std::is_same_v<T, OirCallInst> || std::is_same_v<T, LirCallInst>) {
                            if (fn.name == "main" && inst.callee == "helper")
                                for (const auto& arg : inst.args) out.helperActuals.push_back(arg.bindingKey);
                        }
                    }, instruction);
                }
            }
        }, declaration);
    }
    return out;
}
struct Run {
    std::unordered_map<std::string, std::string> formals;
    std::unordered_map<std::string, std::string> locals;
    std::unordered_map<std::string, int> ownershipDrops;
    Stage oir, lir;
};
Run inspect(RealmDecl* realm, SemanticAnalyzer& sema, bool canonical) {
    Run out;
    Lexical lexical;
    std::unordered_set<std::string> unique;
    lexical.locals = &out.locals;
    for (auto& declaration : realm->declarations) if (auto* fn = dynamic_cast<FnDecl*>(declaration.get())) {
        lexical.functionName = fn->name;
        lexical.enter();
        for (auto& param : fn->params) {
            out.formals[fn->name + "." + param.name] = param.backendBindingKey;
            need(canonical ? !param.backendBindingKey.empty() : param.backendBindingKey.empty(),
                 "formal opt-in/default policy changed: " + fn->name + "." + param.name);
            if (canonical) need(unique.insert(param.backendBindingKey).second, "duplicate formal key");
            lexical.scopes.back()[param.name] = &param.backendBindingKey;
        }
        walkBlock(fn->body.get(), lexical);
        lexical.leave();
    }
    need(lexical.resolvedUses >= 10, "fixture did not resolve the expected declaration uses");
    if (canonical) for (const auto& [_, key] : out.locals)
        need(!key.empty() && unique.insert(key).second, "local aliases another source declaration");
    OwnershipChecker ownership;
    ownership.check(realm, sema);
    const auto& result = ownership.result();
    for (const auto& [_, drops] : result.dropsAtBlockEnd)
        for (const auto& drop : drops) ++out.ownershipDrops[drop.bindingKey];
    for (const auto& [_, drops] : result.dropsBeforeStmt)
        for (const auto& drop : drops) ++out.ownershipDrops[drop.bindingKey];
    auto oir = buildOirProgram(realm->name, {{realm, &sema, &result}});
    out.oir = collect(oir);
    out.lir = collect(buildLirProgram(oir));
    return out;
}
Run analyze(const std::filesystem::path& source, bool canonical) {
    claw::workspace::ProjectLoader loader;
    auto project = loader.load(source);
    auto& unit = project.units[project.entryIndex];
    SemanticAnalyzer sema(unit.importedBindings, project.target, canonical);
    sema.analyze(unit.ast.get());
    return inspect(unit.ast.get(), sema, canonical);
}
void expectReject(const std::filesystem::path& source, const std::string& text, const std::string& fragment) {
    std::ofstream(source) << text;
    bool rejected = false;
    try {
        claw::workspace::ProjectLoader loader;
        auto project = loader.load(source);
        auto& unit = project.units[project.entryIndex];
        SemanticAnalyzer sema(unit.importedBindings, project.target, true);
        sema.analyze(unit.ast.get());
        OwnershipChecker ownership;
        ownership.check(unit.ast.get(), sema);
    } catch (const DiagnosticError& error) {
        for (const auto& diagnostic : error.diagnostics())
            if (diagnostic.message.find(fragment) != std::string::npos) rejected = true;
    }
    need(rejected, "negative fixture did not produce expected diagnostic: " + fragment);
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        const std::filesystem::path artifacts(argv[1]);
        std::filesystem::create_directories(artifacts);
        const auto source = artifacts / "parameter_binding_identity.cat";
        std::ofstream(source) << R"CAT(fn helper(input: String) {
    println(input.as_str())
    if true {
        val input = String.from("local")
        println(input.as_str())
    } else {
        println(input.as_str())
    }
}
fn forwarded(forwarded: String) -> String { return forwarded }
fn owned(value: String) { println(value.as_str()) if false { return } }
fn refs(view: ref String, number: Int32) { println(view.as_str()) println(number) }
fn builtin_shadow(println: Int32) { val ignored = println + 1 }
fn main() {
    val actual = String.from("actual")
    helper(actual)
    val moved = String.from("moved")
    val forwarded_value = forwarded(moved)
    val owned_arg = String.from("owned")
    owned(owned_arg)
    val ref_arg = String.from("ref")
    refs(ref ref_arg, 7)
}
)CAT";
        const auto off = analyze(source, false);
        auto on = analyze(source, true);
        const auto again = analyze(source, true);
        need(on.formals.size() == 6, "formal fixture count changed");
        need(on.formals == again.formals, "fresh analysis changed key correspondence");
        const auto formal = on.formals.at("helper.input");
        const auto actual = on.locals.at("main.actual");
        const auto shadow = on.locals.at("helper.shadow");
        need(formal != actual && formal != shadow, "actual/shadow aliases formal declaration");
        for (Stage* stage : {&on.oir, &on.lir}) {
            for (const auto& [qualified, key] : on.formals) {
                const auto split = qualified.find('.');
                const auto fn = qualified.substr(0, split), name = qualified.substr(split + 1);
                bool found = false;
                for (const auto& [paramName, paramKey] : stage->formals.at(fn))
                    if (paramName == name) { need(paramKey == key, "stage formal key mismatch: " + qualified); found = true; }
                need(found, "stage omitted formal: " + qualified);
            }
            need(stage->helperActuals == std::vector<std::string>{actual}, "call actual lost its declaration key");
            need(stage->returned.count(on.formals.at("forwarded.forwarded")), "moved formal return lost key");
            need(stage->drops[on.formals.at("owned.value")] >= 2, "normal/early formal cleanup missing");
            for (const auto& key : {formal, shadow, on.locals.at("main.forwarded_value"), on.locals.at("main.ref_arg")})
                need(stage->drops[key] > 0, "exact owned cleanup missing: " + key);
            for (const auto& key : {on.formals.at("forwarded.forwarded"), on.formals.at("refs.view"),
                    on.formals.at("refs.number"), on.formals.at("builtin_shadow.println"), actual,
                    on.locals.at("main.moved"), on.locals.at("main.owned_arg")})
                need(stage->drops[key] == 0, "moved/ref/scalar declaration was dropped: " + key);
        }
        need(on.ownershipDrops[on.formals.at("owned.value")] >= 2, "ownership normal/early formal cleanup missing");
        for (const auto& key : {formal, shadow, on.locals.at("main.forwarded_value"), on.locals.at("main.ref_arg")})
            need(on.ownershipDrops[key] > 0, "ownership exact owned cleanup missing: " + key);
        for (const auto& key : {on.formals.at("forwarded.forwarded"), on.formals.at("refs.view"),
                on.formals.at("refs.number"), actual, on.locals.at("main.moved"), on.locals.at("main.owned_arg")})
            need(on.ownershipDrops[key] == 0, "ownership dropped moved/ref/scalar declaration: " + key);

        // Metadata belongs to an analysis result, not permanently to the AST.
        // A fresh default-off analyzer must clear prior opt-in formal/use keys.
        claw::workspace::ProjectLoader loader;
        auto project = loader.load(source);
        auto& unit = project.units[project.entryIndex];
        SemanticAnalyzer enabled(unit.importedBindings, project.target, true);
        enabled.analyze(unit.ast.get());
        inspect(unit.ast.get(), enabled, true);
        SemanticAnalyzer disabled(unit.importedBindings, project.target, false);
        disabled.analyze(unit.ast.get());
        need(inspect(unit.ast.get(), disabled, false).formals == off.formals, "same-AST default analysis retained formal keys");

        const auto negative = artifacts / "negative.cat";
        expectReject(negative, "fn bad(x: Int32) { x = 2 }\nfn main() {}\n", "immutable");
        expectReject(negative, "fn dup(x: Int32, x: Int32) {}\nfn main() {}\n", "Duplicate parameter");
        expectReject(negative, "fn moved(x: String) { val y = x println(x.as_str()) }\nfn main() {}\n", "moved");
        std::cout << "PASS parameter identity: exact lexical declarations, all OIR/LIR formal keys, actual/formal separation, normal/early cleanup, moved/ref/scalar non-drops, default reset and negative diagnostics\n";
        return 0;
    } catch (const DiagnosticError& error) {
        for (const auto& diagnostic : error.diagnostics()) std::cerr << diagnostic.message << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
    }
    return 1;
}
