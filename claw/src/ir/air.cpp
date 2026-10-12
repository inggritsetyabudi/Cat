#include "ir/air.h"

#include "analysis/sema.h"
#include "ast/ast.h"

#include <sstream>

namespace claw::frontend {

namespace {

std::string formatPickPattern(const PickPattern* pattern) {
    if (!pattern) {
        return "<pattern>";
    }
    if (pattern->kind == PickPatternKind::Wildcard) {
        return "_";
    }
    if (pattern->kind == PickPatternKind::Binding) {
        return pattern->name;
    }

    std::string text = "(";
    for (size_t i = 0; i < pattern->elements.size(); ++i) {
        if (i > 0) {
            text += ", ";
        }
        text += formatPickPattern(pattern->elements[i].get());
    }
    if (pattern->elements.size() == 1) {
        text += ",";
    }
    text += ")";
    return text;
}

} // namespace

AirEmitter::AirEmitter(const SemanticAnalyzer& sema) : sema(sema) {}

std::string AirEmitter::emit(const RealmDecl* realm) const {
    if (!realm) {
        return "air.module <unknown>\n";
    }

    std::ostringstream out;
    out << "air.module " << realm->name << "\n";

    for (const auto& imp : realm->imports) {
        out << "import " << imp.modulePath;
        if (!imp.items.empty()) {
            out << ".{";
            for (size_t i = 0; i < imp.items.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << imp.items[i].name;
                if (!imp.items[i].alias.empty()) {
                    out << " as " << imp.items[i].alias;
                }
            }
            out << "}";
        }
        out << "\n";
    }

    for (const auto& decl : realm->declarations) {
        out << emitDecl(decl.get(), 0);
    }

    return out.str();
}

std::string AirEmitter::emitDecl(const Decl* decl, int indent) const {
    if (auto* fn = dynamic_cast<const FnDecl*>(decl)) {
        return emitFn(fn, indent);
    }
    if (auto* shape = dynamic_cast<const ShapeDecl*>(decl)) {
        return emitShape(shape, indent);
    }
    if (auto* choice = dynamic_cast<const ChoiceDecl*>(decl)) {
        return emitChoice(choice, indent);
    }
    return {};
}

std::string AirEmitter::emitFn(const FnDecl* fn, int indent) const {
    std::ostringstream out;
    const auto* signature = sema.lookupFunctionSignature(fn);

    out << indentText(indent) << "air.fn " << fn->name << "(";
    for (size_t i = 0; i < fn->params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << fn->params[i].name << ": ";
        if (signature && i < signature->paramTypes.size()) {
            out << signature->paramTypes[i].describe();
        } else {
            out << "<unknown>";
        }
    }
    out << ") -> ";
    out << (signature ? signature->returnType.describe() : "<unknown>") << "\n";

    if (fn->body) {
        const ExprStmt* implicitTailExpr = nullptr;
        if (!fn->body->statements.empty()) {
            auto* candidate = dynamic_cast<const ExprStmt*>(fn->body->statements.back().get());
            const auto* candidateType = candidate && candidate->expr ? sema.lookupExprType(candidate->expr.get()) : nullptr;
            const bool isUnitControlStatement = candidateType && candidateType->name == "Unit" && candidate &&
                (dynamic_cast<const IfExpr*>(candidate->expr.get()) || dynamic_cast<const PickExpr*>(candidate->expr.get()));
            if (!isUnitControlStatement) {
                implicitTailExpr = candidate;
            }
        }

        for (const auto& stmt : fn->body->statements) {
            if (implicitTailExpr && stmt.get() == implicitTailExpr) {
                const auto* tailType = implicitTailExpr->expr ? sema.lookupExprType(implicitTailExpr->expr.get()) : nullptr;
                if (tailType && tailType->name == "Never") {
                    out << emitStmt(implicitTailExpr, indent + 1);
                } else {
                    out << indentText(indent + 1) << "return ";
                    if (implicitTailExpr->expr) {
                        out << emitExpr(implicitTailExpr->expr.get());
                    } else {
                        out << "unit";
                    }
                    out << "\n";
                }
            } else {
                out << emitStmt(stmt.get(), indent + 1);
            }
        }
    }
    return out.str();
}

std::string AirEmitter::emitShape(const ShapeDecl* shape, int indent) const {
    std::ostringstream out;
    out << indentText(indent) << "air." << (shape->isViewShape ? "view_shape " : "shape ")
        << shape->name;
    if (shape->isViewShape && !shape->scopeParamName.empty()) {
        out << "[" << shape->scopeParamName << "]";
    }
    out << "\n";
    const auto* info = sema.lookupShape(shape->name);
    if (!info) {
        return out.str();
    }

    for (const auto& fieldName : info->fieldOrder) {
        auto it = info->fields.find(fieldName);
        if (it == info->fields.end()) {
            continue;
        }
        out << indentText(indent + 1) << fieldName << ": " << it->second.describe() << "\n";
    }
    return out.str();
}

std::string AirEmitter::emitChoice(const ChoiceDecl* choice, int indent) const {
    std::ostringstream out;
    out << indentText(indent) << "air.choice " << choice->name << "\n";
    const auto* info = sema.lookupChoice(choice->name);
    if (!info) {
        return out.str();
    }

    for (const auto& variantName : info->variantOrder) {
        out << indentText(indent + 1) << variantName;
        auto it = info->variants.find(variantName);
        if (it != info->variants.end() && !it->second.payloadTypes.empty()) {
            out << "(";
            for (size_t i = 0; i < it->second.payloadTypes.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << it->second.payloadTypes[i].describe();
            }
            out << ")";
        }
        out << "\n";
    }
    return out.str();
}

std::string AirEmitter::emitBlock(const BlockStmt* block, int indent) const {
    std::ostringstream out;
    for (const auto& stmt : block->statements) {
        out << emitStmt(stmt.get(), indent);
    }
    return out.str();
}

std::string AirEmitter::emitStmt(const Stmt* stmt, int indent) const {
    std::ostringstream out;
    const std::string pad = indentText(indent);

    if (auto* bind = dynamic_cast<const BindingStmt*>(stmt)) {
        if (!bind->tuplePatternNames.empty()) {
            out << pad << "val (";
            for (size_t i = 0; i < bind->tuplePatternNames.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << bind->tuplePatternNames[i];
            }
            out << ") = " << emitExpr(bind->value.get()) << "\n";
            return out.str();
        }

        const auto bindingIt = sema.result().bindingTypes.find(bind);
        out << pad << (bind->isMutable ? "var " : "val ") << bind->name << ": "
            << (bindingIt != sema.result().bindingTypes.end() ? bindingIt->second.describe() : "<unknown>");
        if (bind->value) {
            out << " = " << emitExpr(bind->value.get());
        }
        out << "\n";
        return out.str();
    }

    if (auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        out << pad << "assign " << emitExpr(assign->target.get());
        if (assign->op == "=") {
            out << " <- ";
        } else {
            out << " " << assign->op << " ";
        }
        out << emitExpr(assign->value.get()) << "\n";
        return out.str();
    }

    if (auto* returnStmt = dynamic_cast<const ReturnStmt*>(stmt)) {
        out << pad << "return";
        if (returnStmt->value) {
            out << " " << emitExpr(returnStmt->value.get());
        }
        out << "\n";
        return out.str();
    }

    if (auto* exprStmt = dynamic_cast<const ExprStmt*>(stmt)) {
        if (auto* ifExpr = dynamic_cast<const IfExpr*>(exprStmt->expr.get())) {
            out << pad << "if " << emitExpr(ifExpr->condition.get()) << "\n";
            out << emitBlock(ifExpr->thenBlock.get(), indent + 1);
            if (ifExpr->elseBlock) {
                out << pad << "else\n";
                out << emitBlock(ifExpr->elseBlock.get(), indent + 1);
            }
            return out.str();
        }
        if (auto* pickExpr = dynamic_cast<const PickExpr*>(exprStmt->expr.get())) {
            out << pad << "pick " << emitExpr(pickExpr->value.get()) << "\n";
            for (const auto& branch : pickExpr->branches) {
                out << indentText(indent + 1)
                    << (branch.tuplePattern ? formatPickPattern(branch.tuplePattern.get()) : branch.tag);
                if (!branch.tuplePattern && !branch.bindings.empty()) {
                    out << "(";
                    for (size_t i = 0; i < branch.bindings.size(); ++i) {
                        if (i > 0) out << ", ";
                        out << branch.bindings[i];
                    }
                    out << ")";
                }
                out << "\n" << emitBlock(branch.body.get(), indent + 2);
            }
            return out.str();
        }
        out << pad << "expr " << emitExpr(exprStmt->expr.get()) << "\n";
        return out.str();
    }

    if (auto* ifStmt = dynamic_cast<const IfStmt*>(stmt)) {
        out << pad << "if " << emitExpr(ifStmt->condition.get()) << "\n";
        out << emitBlock(ifStmt->thenBlock.get(), indent + 1);
        if (ifStmt->elseBlock) {
            out << pad << "else\n";
            out << emitBlock(ifStmt->elseBlock.get(), indent + 1);
        }
        return out.str();
    }

    if (auto* loop = dynamic_cast<const LoopStmt*>(stmt)) {
        out << pad << "loop";
        if (loop->condition) {
            out << " " << emitExpr(loop->condition.get());
        }
        out << "\n";
        out << emitBlock(loop->body.get(), indent + 1);
        return out.str();
    }

    if (auto* scan = dynamic_cast<const ScanStmt*>(stmt)) {
        const auto itemIt = sema.result().scanItemTypes.find(scan);
        out << pad << "scan " << scan->itemName << ": "
            << (itemIt != sema.result().scanItemTypes.end() ? itemIt->second.describe() : "<unknown>");
        if (!scan->indexName.empty()) {
            out << ", " << scan->indexName << ": USize";
        }
        out << " over " << emitExpr(scan->iterable.get()) << "\n";
        out << emitBlock(scan->body.get(), indent + 1);
        return out.str();
    }

    if (auto* scopeStmt = dynamic_cast<const ScopeStmt*>(stmt)) {
        out << pad << "scope " << scopeStmt->name << "\n";
        out << emitBlock(scopeStmt->body.get(), indent + 1);
        return out.str();
    }

    if (auto* pick = dynamic_cast<const PickStmt*>(stmt)) {
        out << pad << "pick " << emitExpr(pick->value.get()) << "\n";
        for (const auto& branch : pick->branches) {
            out << indentText(indent + 1)
                << (branch.tuplePattern ? formatPickPattern(branch.tuplePattern.get()) : branch.tag);
            if (!branch.tuplePattern && !branch.bindings.empty()) {
                out << "(";
                for (size_t i = 0; i < branch.bindings.size(); ++i) {
                    if (i > 0) {
                        out << ", ";
                    }
                    out << branch.bindings[i];
                }
                out << ")";
            }
            out << "\n";
            out << emitBlock(branch.body.get(), indent + 2);
        }
        return out.str();
    }

    if (auto* tryStmt = dynamic_cast<const TryStmt*>(stmt)) {
        out << pad << (tryStmt->isMutable ? "var " : "val ") << tryStmt->name;
        if (tryStmt->type) {
            out << ": " << tryStmt->type->name;
            if (!tryStmt->type->params.empty()) {
                out << "[...]";
            }
        }
        out << " = try " << emitExpr(tryStmt->expr.get());
        if (tryStmt->autoPropagate) {
            out << "\n";
        } else {
            out << " else " << tryStmt->failName << "\n";
            out << emitBlock(tryStmt->failBlock.get(), indent + 1);
        }
        return out.str();
    }

    if (auto* raw = dynamic_cast<const RawStmt*>(stmt)) {
        out << pad << "raw\n";
        out << emitBlock(raw->body.get(), indent + 1);
        return out.str();
    }

    if (auto* scope = dynamic_cast<const ScopeStmt*>(stmt)) {
        out << pad << "scope " << scope->name << "\n";
        out << emitBlock(scope->body.get(), indent + 1);
        return out.str();
    }

    if (dynamic_cast<const StopStmt*>(stmt)) {
        out << pad << "stop\n";
        return out.str();
    }

    if (dynamic_cast<const SkipStmt*>(stmt)) {
        out << pad << "skip\n";
        return out.str();
    }

    return out.str();
}

std::string AirEmitter::emitExpr(const Expr* expr) const {
    return emitExprValue(expr) + " : " + emitTypeOf(expr);
}

std::string AirEmitter::emitExprValue(const Expr* expr) const {
    if (!expr) {
        return "<none>";
    }

    std::ostringstream out;

    if (auto* value = dynamic_cast<const IntExpr*>(expr)) {
        out << value->value;
    } else if (auto* value = dynamic_cast<const CharExpr*>(expr)) {
        out << value->value;
    } else if (auto* value = dynamic_cast<const FloatExpr*>(expr)) {
        out << value->value;
    } else if (auto* value = dynamic_cast<const StringExpr*>(expr)) {
        out << '"' << value->value << '"';
    } else if (auto* tuple = dynamic_cast<const TupleExpr*>(expr)) {
        out << "(";
        for (size_t i = 0; i < tuple->elements.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << emitExprValue(tuple->elements[i].get());
        }
        if (tuple->elements.size() == 1) {
            out << ",";
        }
        out << ")";
    } else if (auto* value = dynamic_cast<const BoolExpr*>(expr)) {
        out << (value->value ? "true" : "false");
    } else if (auto* ifExpr = dynamic_cast<const IfExpr*>(expr)) {
        out << "if " << emitExpr(ifExpr->condition.get()) << " {\n"
            << emitBlock(ifExpr->thenBlock.get(), 1) << "}";
        if (ifExpr->elseBlock) {
            out << " else {\n" << emitBlock(ifExpr->elseBlock.get(), 1) << "}";
        }
    } else if (auto* pickExpr = dynamic_cast<const PickExpr*>(expr)) {
        out << "pick " << emitExpr(pickExpr->value.get()) << " {\n";
        for (const auto& branch : pickExpr->branches) {
            out << indentText(1)
                << (branch.tuplePattern ? formatPickPattern(branch.tuplePattern.get()) : branch.tag);
            if (!branch.tuplePattern && !branch.bindings.empty()) {
                out << "(";
                for (size_t i = 0; i < branch.bindings.size(); ++i) {
                    if (i > 0) out << ", ";
                    out << branch.bindings[i];
                }
                out << ")";
            }
            out << " {\n" << emitBlock(branch.body.get(), 2) << indentText(1) << "}\n";
        }
        out << "}";
    } else if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        out << ident->name;
    } else if (auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        out << "(" << unary->op << " " << emitExprValue(unary->operand.get()) << ")";
    } else if (auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        out << "(" << emitExprValue(binary->left.get()) << " " << binary->op << " " << emitExprValue(binary->right.get()) << ")";
    } else if (auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
        out << "ref";
        if (borrow->isMutable) {
            out << " mut";
        }
        if (!borrow->scopeName.empty()) {
            out << "[" << borrow->scopeName << "]";
        }
        out << " " << emitExprValue(borrow->target.get());
    } else if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        out << emitExprValue(index->object.get()) << "[" << emitExpr(index->index.get()) << "]";
    } else if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        out << emitExprValue(call->callee.get()) << "(";
        for (size_t i = 0; i < call->args.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << emitExpr(call->args[i].get());
        }
        out << ")";
    } else if (auto* shapeInit = dynamic_cast<const ShapeInitExpr*>(expr)) {
        out << shapeInit->name;
        if (!shapeInit->scopeName.empty()) {
            out << "[" << shapeInit->scopeName << "]";
        }
        out << " {";
        for (size_t i = 0; i < shapeInit->fields.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << shapeInit->fields[i].name << ": " << emitExpr(shapeInit->fields[i].value.get());
        }
        out << "}";
    } else if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        out << emitExprValue(member->object.get()) << "." << member->member;
    } else {
        out << "<expr>";
    }

    return out.str();
}

std::string AirEmitter::emitTypeOf(const Expr* expr) const {
    const auto* type = sema.lookupExprType(expr);
    return type ? type->describe() : std::string("<unknown>");
}

std::string AirEmitter::indentText(int indent) const {
    return std::string(static_cast<size_t>(indent) * 2, ' ');
}

} // namespace claw::frontend
