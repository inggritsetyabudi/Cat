#include "analysis/sema.h"
#include "analysis/builtin_methods.h"
#include "analysis/choice_utils.h"
#include "analysis/numeric_literals.h"

#include "ast/ast.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace claw::frontend {

using namespace numeric_literals;

namespace {

ResolvedType makePlainType(const std::string& name) {
    ResolvedType type;
    type.name = name;
    type.category = TypeCategory::Plain;
    return type;
}

ResolvedType makeOwnedType(const std::string& name) {
    ResolvedType type;
    type.name = name;
    type.category = TypeCategory::Owned;
    return type;
}

ResolvedType makeStrViewType() {
    ResolvedType type;
    type.name = "Str";
    type.viewKind = "look";
    type.category = TypeCategory::View;
    return type;
}

bool isInt32RangeType(const ResolvedType& type) {
    return type.name == "Range" && type.isPlain() && type.params.size() == 1 &&
        type.params.front().name == "Int32" && type.params.front().isPlain() &&
        type.params.front().params.empty();
}
bool isSupportedFixedArrayType(const ResolvedType& type) {
    return type.name == "Array" && type.isPlain() && type.params.size() == 2 &&
        (type.params.front().name == "Int32" || type.params.front().name == "UInt8") &&
        type.params.front().isPlain() && type.params.front().params.empty() &&
        type.params[1].isPlain();
}
bool isBorrowedStrMaybeType(const ResolvedType& type) {
    return type.name == "Maybe" && type.params.size() == 1 &&
        type.params.front().name == "Str" && type.params.front().isView() &&
        type.params.front().viewKind == "look" && type.params.front().params.empty();
}

bool hasBorrowedStorageType(const ResolvedType& type) {
    if (type.isView() || !type.scopeName.empty() || !type.viewScope.empty() || type.name == "Span") {
        return true;
    }
    return std::any_of(type.params.begin(), type.params.end(), hasBorrowedStorageType);
}

std::string stringOwnershipHint(const ResolvedType& expected, const ResolvedType& actual) {
    const std::string expectedName = canonicalTypeName(expected.name);
    const std::string actualName = canonicalTypeName(actual.name);
    if (expectedName == "String" && actualName == "Str") {
        return ". Hint: use `String.from(str)` when an owned copy is required.";
    }
    if (expectedName == "Str" && actualName == "String") {
        return ". Hint: call `value.as_str()` for a borrowed view and keep the owner alive for the full use.";
    }
    return {};
}

void inheritMaybeStrViewScope(ResolvedType& maybeType, const ResolvedType& sourceType) {
    if (isBorrowedStrMaybeType(maybeType) && maybeType.params.front().viewScope.empty()) {
        maybeType.params.front().viewScope = sourceType.viewScope;
    }
}

bool isPlainTupleDestructuringField(const ResolvedType& type) {
    if (!type.isPlain() || type.isOwned() || type.isView() || type.name == "Span" || type.isUnknown() ||
        type.isOpaqueExternal() || type.isGeneric || !type.scopeName.empty() || !type.viewScope.empty()) {
        return false;
    }
    for (const auto& parameter : type.params) {
        if (!isPlainTupleDestructuringField(parameter)) {
            return false;
        }
    }
    return true;
}

bool isSupportedTupleDestructuringField(const ResolvedType& type) {
    if (isPlainTupleDestructuringField(type)) {
        return true;
    }
    if (type.isView() || type.isUnknown() || type.isOpaqueExternal() || type.isGeneric ||
        !type.scopeName.empty() || !type.viewScope.empty()) {
        return false;
    }
    if (type.name == "Tuple" && type.isOwned()) {
        return std::all_of(type.params.begin(), type.params.end(), isSupportedTupleDestructuringField);
    }
    if (type.name == "Anchor" && type.isOwned() && type.params.size() == 1) {
        return isPlainTupleDestructuringField(type.params.front());
    }
    return false;
}

bool hasNestedTuplePattern(const PickPattern* pattern) {
    if (!pattern || pattern->kind != PickPatternKind::Tuple) {
        return false;
    }
    return std::any_of(pattern->elements.begin(), pattern->elements.end(), [](const auto& element) {
        return element && (element->kind == PickPatternKind::Tuple || hasNestedTuplePattern(element.get()));
    });
}

bool containsOwnedTupleValue(const ResolvedType& type) {
    if (type.name == "Tuple" && type.isOwned()) {
        return true;
    }
    return std::any_of(type.params.begin(), type.params.end(), containsOwnedTupleValue);
}

std::optional<size_t> parseTupleProjectionIndex(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    size_t index = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return index;
}

bool startsWithUppercase(const std::string& name) {
    return !name.empty() && std::isupper(static_cast<unsigned char>(name.front())) != 0;
}

bool startsWithLowercase(const std::string& name) {
    return !name.empty() && std::islower(static_cast<unsigned char>(name.front())) != 0;
}

bool isRawAddressTypeName(const std::string& name) {
    return name == "Addr" || name == "RawPtr" || name == "RawMut";
}

std::string describeCalleeExpr(const Expr* expr) {
    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        return ident->name;
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return describeCalleeExpr(member->object.get()) + "." + member->member;
    }

    return "<callee>";
}

bool isArenaNewCall(const Expr* callee) {
    const auto* member = dynamic_cast<const MemberExpr*>(callee);
    if (!member) {
        return false;
    }
    const auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get());
    return objectIdent && objectIdent->name == "Arena" && member->member == "new";
}

bool isAnchorStaticConstructorCall(const Expr* callee) {
    const auto* member = dynamic_cast<const MemberExpr*>(callee);
    if (!member) {
        return false;
    }
    const auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get());
    return objectIdent && objectIdent->name == "Anchor" && member->member == "new";
}

bool isStringFromCall(const Expr* callee) {
    const auto* member = dynamic_cast<const MemberExpr*>(callee);
    if (!member) {
        return false;
    }
    const auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get());
    return objectIdent && objectIdent->name == "String" && member->member == "from";
}

bool isStaticStringLiteral(const Expr* expr) {
    return dynamic_cast<const StringExpr*>(expr) != nullptr;
}

bool isOwnedTemporaryExpression(const SemanticAnalyzer& sema, const Expr* expr) {
    const auto* type = sema.lookupExprType(expr);
    return type && type->isOwned() && type->name != "Arena" &&
        !sema.isPlaceExpression(expr) && !isStaticStringLiteral(expr);
}

bool projectsFromOwnedTemporary(const SemanticAnalyzer& sema, const Expr* expr) {
    const Expr* owner = nullptr;
    if (const auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        owner = member->object.get();
    } else if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        owner = index->object.get();
    }

    if (!owner || sema.isPlaceExpression(owner) || isStaticStringLiteral(owner)) {
        return false;
    }
    const auto* ownerType = sema.lookupExprType(owner);
    return ownerType && ownerType->isOwned() && ownerType->name != "Arena";
}

bool callsBorrowingMethodOnOwnedTemporary(const SemanticAnalyzer& sema, const Expr* expr) {
    const auto* call = dynamic_cast<const CallExpr*>(expr);
    const auto* member = call ? dynamic_cast<const MemberExpr*>(call->callee.get()) : nullptr;
    if (!member) {
        return false;
    }

    const auto method = sema.lookupMethodSignature(call->callee.get());
    const Expr* receiver = member->object.get();
    if (!method || !method->receiverType.isView()) {
        return false;
    }
    const bool returnsBorrowFromReceiver = method->viewReturnFromReceiver ||
        (method->function.viewReturnSourceParam.has_value() && *method->function.viewReturnSourceParam == SIZE_MAX) ||
        (method->function.scopeReturnSourceParam.has_value() && *method->function.scopeReturnSourceParam == SIZE_MAX);
    return returnsBorrowFromReceiver && isOwnedTemporaryExpression(sema, receiver);
}

bool callReturnsBorrowFromOwnedTemporaryArgument(
    const SemanticAnalyzer& sema,
    const CallExpr* call,
    const FunctionSignature* signature,
    const MethodSignature* method) {
    if (!call) {
        return false;
    }

    for (size_t i = 0; i < call->args.size(); ++i) {
        if (!isOwnedTemporaryExpression(sema, call->args[i].get())) {
            continue;
        }
        const bool signatureReturnsBorrowFromArg = signature &&
            ((signature->viewReturnSourceParam.has_value() && *signature->viewReturnSourceParam == i) ||
             (signature->scopeReturnSourceParam.has_value() && *signature->scopeReturnSourceParam == i));
        const bool methodReturnsBorrowFromArg = method &&
            method->viewReturnSourceArg.has_value() && *method->viewReturnSourceArg == i;
        if (signatureReturnsBorrowFromArg || methodReturnsBorrowFromArg) {
            return true;
        }
    }
    return false;
}

bool expressionReturnsBorrowFromOwnedTemporary(const SemanticAnalyzer& sema, const Expr* expr);

bool blockTailReturnsBorrowFromOwnedTemporary(const SemanticAnalyzer& sema, const BlockStmt* block) {
    if (!block || block->statements.empty()) {
        return false;
    }
    const auto* tail = dynamic_cast<const ExprStmt*>(block->statements.back().get());
    return tail && expressionReturnsBorrowFromOwnedTemporary(sema, tail->expr.get());
}

bool expressionReturnsBorrowFromOwnedTemporary(const SemanticAnalyzer& sema, const Expr* expr) {
    if (!expr) {
        return false;
    }
    const ResolvedType* resultType = sema.lookupExprType(expr);
    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        const auto* member = dynamic_cast<const MemberExpr*>(call->callee.get());
        const auto method = member ? sema.lookupMethodSignature(call->callee.get()) : std::nullopt;
        const FunctionSignature* signature = method.has_value()
            ? &method->function
            : sema.lookupCallableSignature(call->callee.get());
        if (method.has_value() && member && resultType && hasBorrowedStorageType(*resultType) &&
            callsBorrowingMethodOnOwnedTemporary(sema, call)) {
            return true;
        }
        if (callReturnsBorrowFromOwnedTemporaryArgument(
                sema,
                call,
                signature,
                method.has_value() ? &*method : nullptr)) {
            return true;
        }
        if (!resultType || !hasBorrowedStorageType(*resultType)) {
            return false;
        }
        const bool returnsBorrowFromReceiver = method.has_value() &&
            (method->viewReturnFromReceiver ||
             (method->function.viewReturnSourceParam.has_value() &&
              *method->function.viewReturnSourceParam == SIZE_MAX) ||
             (method->function.scopeReturnSourceParam.has_value() &&
              *method->function.scopeReturnSourceParam == SIZE_MAX));
        if (returnsBorrowFromReceiver && member &&
            expressionReturnsBorrowFromOwnedTemporary(sema, member->object.get())) {
            return true;
        }
        for (size_t i = 0; i < call->args.size(); ++i) {
            const bool returnsBorrowFromArg =
                (signature &&
                 ((signature->viewReturnSourceParam.has_value() && *signature->viewReturnSourceParam == i) ||
                  (signature->scopeReturnSourceParam.has_value() && *signature->scopeReturnSourceParam == i))) ||
                (method.has_value() && method->viewReturnSourceArg.has_value() && *method->viewReturnSourceArg == i);
            if (returnsBorrowFromArg && expressionReturnsBorrowFromOwnedTemporary(sema, call->args[i].get())) {
                return true;
            }
        }
        return false;
    }
    if (!resultType || !hasBorrowedStorageType(*resultType)) {
        return false;
    }
    if (const auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return expressionReturnsBorrowFromOwnedTemporary(sema, member->object.get());
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return expressionReturnsBorrowFromOwnedTemporary(sema, index->object.get());
    }
    if (const auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
        return isOwnedTemporaryExpression(sema, borrow->target.get()) ||
            expressionReturnsBorrowFromOwnedTemporary(sema, borrow->target.get());
    }
    if (const auto* tuple = dynamic_cast<const TupleExpr*>(expr)) {
        return std::any_of(tuple->elements.begin(), tuple->elements.end(), [&](const auto& element) {
            return expressionReturnsBorrowFromOwnedTemporary(sema, element.get());
        });
    }
    if (const auto* array = dynamic_cast<const ArrayExpr*>(expr)) {
        return std::any_of(array->elements.begin(), array->elements.end(), [&](const auto& element) {
            return expressionReturnsBorrowFromOwnedTemporary(sema, element.get());
        });
    }
    if (const auto* shape = dynamic_cast<const ShapeInitExpr*>(expr)) {
        return std::any_of(shape->fields.begin(), shape->fields.end(), [&](const ShapeInitField& field) {
            return expressionReturnsBorrowFromOwnedTemporary(sema, field.value.get());
        });
    }
    return false;
}

bool containsAnchorPayload(const ResolvedType& type) {
    if (canonicalTypeName(type.name) == "Anchor") {
        return true;
    }
    return std::any_of(type.params.begin(), type.params.end(), containsAnchorPayload);
}

bool isAnchorPayloadTypeAllowed(const ResolvedType& type) {
    if (type.isUnknown() || type.isOpaqueExternal() || type.isView() ||
        !type.scopeName.empty() || !type.viewScope.empty()) {
        return false;
    }

    if (containsAnchorPayload(type)) {
        return false;
    }

    const std::string base = canonicalTypeName(type.name);
    if (base == "Span" || base == "CStr" || isRawAddressTypeName(base)) {
        return false;
    }
    return std::all_of(type.params.begin(), type.params.end(), isAnchorPayloadTypeAllowed);
}

std::string anchorPayloadTypeError(const ResolvedType& type) {
    if (containsAnchorPayload(type)) {
        return "Anchor.new(...) cannot store payload type `" + type.describe() +
            "` because it contains a nested Anchor; dropping the outer Anchor does not recursively destroy its payload. Recursive Anchor payload destruction is not supported.";
    }
    if (canonicalTypeName(type.name) == "Str") {
        return "Anchor.new(...) cannot store borrowed Str view `" + type.describe() +
            "`. Hint: create owned text with `String.from(view)` before anchoring it.";
    }
    return "Anchor.new(...) requires an owned payload with stable ownership. " +
        type.describe() + " cannot be anchored directly. Hint: pass an owned value; for text, use `String.from(view)`.";
}

ResolvedType makeOpaqueExternalCallType(const Expr* callee) {
    return makeOpaqueExternalType(describeCalleeExpr(callee));
}

std::string opaqueExternalValueUseMessage(const ResolvedType& type, const std::string& context) {
    const std::string subject = type.name.empty()
        ? std::string("Opaque external call result")
        : "Opaque external call result from '" + type.name + "'";
    return subject + " may only be used as a standalone statement, not in " + context + ".";
}

std::string opaqueExternalRawRequirementMessage(const Expr* callee) {
    return "Opaque external call '" + describeCalleeExpr(callee) +
        "' requires an explicit raw block or a shared typed import contract.";
}

std::string typedExternalRawRequirementMessage(const Expr* callee) {
    return "External call '" + describeCalleeExpr(callee) +
        "' is declared raw-only and requires an explicit raw block.";
}

ResolvedType asViewType(const ResolvedType& base, const std::string& viewKind) {
    ResolvedType adapted = base;
    adapted.viewKind = viewKind;
    adapted.viewScope = base.viewScope;
    adapted.category = viewKind.empty() ? base.category : TypeCategory::View;
    return adapted;
}

} // namespace

SemanticAnalyzer::SemanticAnalyzer(std::vector<ImportedBinding> importedBindings, TargetSpec target)
    : importedBindings(std::move(importedBindings)), target(std::move(target)) {}

const std::vector<Diagnostic>& SemanticAnalyzer::warnings() const {
    return semanticWarnings;
}

const AnalysisResult& SemanticAnalyzer::result() const {
    return analysisResult;
}

const ResolvedType* SemanticAnalyzer::lookupExprType(const Expr* expr) const {
    const auto it = analysisResult.exprTypes.find(expr);
    return it != analysisResult.exprTypes.end() ? &it->second : nullptr;
}

bool SemanticAnalyzer::isPlaceExpression(const Expr* expr) const {
    if (const auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        return analysisResult.choiceConstructors.find(ident) == analysisResult.choiceConstructors.end();
    }
    if (const auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return isPlaceExpression(member->object.get());
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return isPlaceExpression(index->object.get());
    }
    // Current AST forms other than identifiers and projections are rvalues.
    return false;
}

const FunctionSignature* SemanticAnalyzer::lookupFunctionSignature(const FnDecl* fn) const {
    const auto it = analysisResult.functionSignatures.find(fn);
    return it != analysisResult.functionSignatures.end() ? &it->second : nullptr;
}

const FunctionSignature* SemanticAnalyzer::lookupFunctionSignature(const std::string& name) const {
    const auto it = analysisResult.functionsByName.find(name);
    return it != analysisResult.functionsByName.end() ? &it->second : nullptr;
}

const FunctionSignature* SemanticAnalyzer::lookupCallableSignature(const Expr* callee) const {
    if (auto* ident = dynamic_cast<const IdentExpr*>(callee)) {
        return lookupFunctionSignature(ident->name);
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(callee)) {
        if (auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get())) {
            const auto sym = lookupSymbol(objectIdent->name);
            if (sym && sym->kind == SymbolKind::Module && sym->moduleInfo) {
                const auto exported = sym->moduleInfo->exportedItems.find(member->member);
                if (exported != sym->moduleInfo->exportedItems.end() &&
                    exported->second.kind == SymbolKind::Function &&
                    exported->second.functionSignature.has_value()) {
                    return &*exported->second.functionSignature;
                }
            } else if (sym && (sym->kind == SymbolKind::Shape || sym->kind == SymbolKind::Choice)) {
                const std::string qualifiedName = objectIdent->name + "." + member->member;
                return lookupFunctionSignature(qualifiedName);
            }
        }
    }

    return nullptr;
}

const ShapeInfo* SemanticAnalyzer::lookupShape(const std::string& name) const {
    const auto it = analysisResult.shapesByName.find(name);
    if (it != analysisResult.shapesByName.end()) {
        return &it->second;
    }

    const std::string canonical = canonicalTypeName(name);
    if (canonical != name) {
        const auto canonicalIt = analysisResult.shapesByName.find(canonical);
        if (canonicalIt != analysisResult.shapesByName.end()) {
            return &canonicalIt->second;
        }
    }

    return nullptr;
}

std::optional<MethodSignature> SemanticAnalyzer::lookupMethodSignature(const Expr* callee) const {
    auto* member = dynamic_cast<const MemberExpr*>(callee);
    if (!member) {
        return std::nullopt;
    }

    if (auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get())) {
        const auto sym = lookupSymbol(objectIdent->name);
        if (sym && sym->kind == SymbolKind::Module) {
            return std::nullopt;
        }
    }

    const auto* receiverType = lookupExprType(member->object.get());
    if (!receiverType) {
        return std::nullopt;
    }

    return lookupMethodSignature(*receiverType, member->member);
}

const ChoiceInfo* SemanticAnalyzer::lookupChoice(const std::string& name) const {
    const auto it = analysisResult.choicesByName.find(name);
    if (it != analysisResult.choicesByName.end()) {
        return &it->second;
    }

    const std::string canonical = canonicalTypeName(name);
    if (canonical != name) {
        const auto canonicalIt = analysisResult.choicesByName.find(canonical);
        if (canonicalIt != analysisResult.choicesByName.end()) {
            return &canonicalIt->second;
        }
    }

    return nullptr;
}

const TargetSpec& SemanticAnalyzer::targetSpec() const {
    return target;
}

void SemanticAnalyzer::reportError(const std::string& msg) {
    diagnostics.push_back(Diagnostic{"semantic", msg, {}});
}

void SemanticAnalyzer::reportError(const SourceSpan& span, const std::string& msg) {
    diagnostics.push_back(Diagnostic{"semantic", msg, span});
}

void SemanticAnalyzer::reportError(const AstNode* node, const std::string& msg) {
    reportError(node ? node->span : SourceSpan{}, msg);
}

void SemanticAnalyzer::reportWarning(const AstNode* node, const std::string& msg) {
    Diagnostic diagnostic{"semantic", msg, node ? node->span : SourceSpan{}};
    diagnostic.severity = DiagnosticSeverity::Warning;
    semanticWarnings.push_back(std::move(diagnostic));
}

void SemanticAnalyzer::warnNonTailValueExpression(const Stmt* stmt) {
    const auto* expressionStmt = dynamic_cast<const ExprStmt*>(stmt);
    if (!expressionStmt || !expressionStmt->expr) {
        return;
    }

    const Expr* expr = expressionStmt->expr.get();
    const auto* type = lookupExprType(expr);
    if (!type || type->isUnknown() || type->isOpaqueExternal() || typeIsMustUse(*type)) {
        return;
    }

    if (projectsFromOwnedTemporary(*this, expr)) {
        reportError(
            expressionStmt,
            "Cannot discard an expression projected from an owned temporary because its owner cannot be safely destroyed on this path.");
        return;
    }
    if (typeContainsBorrowedStorage(*type) && expressionReturnsBorrowFromOwnedTemporary(*this, expr)) {
        reportError(
            expressionStmt,
            "Cannot discard a borrowed view whose owner is a temporary. Hint: consume it in the same expression, or bind the owner to a local variable.");
        return;
    }
    if (type->name == "Unit" || type->name == "Never") {
        return;
    }

    if (type->isOwned() && !isPlaceExpression(expr) && !isStaticStringLiteral(expr) &&
        type->name != "Anchor" && type->name != "Arena") {
        reportError(
            expressionStmt,
            "Cannot discard owned temporary of type `" + type->describe() +
                "` because no safe destructor is available.");
        return;
    }

    reportWarning(
        expressionStmt,
        "Non-final expression value is ignored; only the final expression in a block returns implicitly.");
}

bool SemanticAnalyzer::typeContainsBorrowedStorage(const ResolvedType& type) const {
    if (type.isView() || !type.scopeName.empty() || !type.viewScope.empty()) {
        return true;
    }

    const std::string base = canonicalTypeName(type.name);
    if (base == "Span") {
        return true;
    }

    for (const auto& param : type.params) {
        if (typeContainsBorrowedStorage(param)) {
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::typeUsesOnlyNamedScope(const ResolvedType& type, std::string_view scopeName) const {
    if (type.isView()) {
        if (type.viewScope.empty() || type.viewScope != scopeName) {
            return false;
        }
    }
    if (!type.scopeName.empty()) {
        if (type.scopeName != scopeName) {
            return false;
        }
    }

    const std::string base = canonicalTypeName(type.name);
    if (base == "Span" && !type.isView()) {
        return false;
    }

    for (const auto& param : type.params) {
        if (typeContainsBorrowedStorage(param) && !typeUsesOnlyNamedScope(param, scopeName)) {
            return false;
        }
    }
    return true;
}

bool SemanticAnalyzer::typeContainsScopeName(const ResolvedType& type) const {
    if (!type.scopeName.empty() || !type.viewScope.empty()) {
        return true;
    }
    for (const auto& param : type.params) {
        if (typeContainsScopeName(param)) {
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::typeIsMustUse(const ResolvedType& type) const {
    const std::string base = canonicalTypeName(type.name);
    return base == "Result" || base == "Maybe";
}

bool SemanticAnalyzer::isImplicitTailExpr(const ExprStmt* stmt) const {
    return stmt && stmt == implicitTailExpr;
}

bool SemanticAnalyzer::isNamedScopeActive(std::string_view scopeName) const {
    return std::find(activeNamedScopes.begin(), activeNamedScopes.end(), scopeName) != activeNamedScopes.end();
}

bool SemanticAnalyzer::symbolCanStoreNamedScope(const Symbol& symbol, std::string_view scopeName) const {
    return std::find(symbol.declaredNamedScopes.begin(), symbol.declaredNamedScopes.end(), scopeName) != symbol.declaredNamedScopes.end();
}

ResolvedType SemanticAnalyzer::bindFormalScopeName(
    const ResolvedType& type,
    std::string_view formalScope,
    std::string_view actualScope) const {
    ResolvedType bound = type;
    if (!formalScope.empty() && bound.scopeName == formalScope) {
        bound.scopeName = std::string(actualScope);
    } else if (actualScope.empty() && bound.scopeName == formalScope) {
        bound.scopeName.clear();
    }
    if (!formalScope.empty() && bound.viewScope == formalScope) {
        bound.viewScope = std::string(actualScope);
    } else if (actualScope.empty() && bound.viewScope == formalScope) {
        bound.viewScope.clear();
    }
    bound.params.clear();
    for (const auto& param : type.params) {
        bound.params.push_back(bindFormalScopeName(param, formalScope, actualScope));
    }
    return bound;
}

bool SemanticAnalyzer::bindSignatureScopes(
    const ResolvedType& expected,
    const ResolvedType& actual,
    const FunctionSignature& signature,
    std::unordered_map<std::string, std::string>& bindings,
    const AstNode* node) {
    auto bindOne = [&](const std::string& formal, const std::string& concrete) {
        if (formal.empty() || std::find(signature.scopeParams.begin(), signature.scopeParams.end(), formal) == signature.scopeParams.end()) {
            return true;
        }
        if (concrete.empty()) {
            reportError(node, "Argument for scope parameter '" + formal + "' must carry a scope-bound value.");
            return false;
        }
        const auto existing = bindings.find(formal);
        if (existing != bindings.end() && existing->second != concrete) {
            reportError(
                node,
                "scope parameter '" + formal + "' is bound to different active scopes ('" + existing->second + "' and '" + concrete + "').");
            return false;
        }
        if (!isNamedScopeActive(concrete)) {
            reportError(node, "Argument scope '" + concrete + "' for scope parameter '" + formal + "' is not active here.");
            return false;
        }
        bindings[formal] = concrete;
        return true;
    };

    bool valid = bindOne(expected.scopeName, actual.scopeName);
    valid = bindOne(expected.viewScope, actual.viewScope) && valid;
    const size_t count = std::min(expected.params.size(), actual.params.size());
    for (size_t i = 0; i < count; ++i) {
        valid = bindSignatureScopes(expected.params[i], actual.params[i], signature, bindings, node) && valid;
    }
    return valid;
}

ResolvedType SemanticAnalyzer::instantiateSignatureType(
    const ResolvedType& type,
    const std::unordered_map<std::string, std::string>& bindings) const {
    ResolvedType instantiated = type;
    const auto scopeName = bindings.find(instantiated.scopeName);
    if (scopeName != bindings.end()) {
        instantiated.scopeName = scopeName->second;
    }
    const auto viewScope = bindings.find(instantiated.viewScope);
    if (viewScope != bindings.end()) {
        instantiated.viewScope = viewScope->second;
    }
    instantiated.params.clear();
    for (const auto& param : type.params) {
        instantiated.params.push_back(instantiateSignatureType(param, bindings));
    }
    return instantiated;
}

void SemanticAnalyzer::enterSemanticScope() {
    scopes.enterScope();
    ++lexicalScopeDepth;
}

void SemanticAnalyzer::exitSemanticScope() {
    if (lexicalScopeDepth > 0) {
        --lexicalScopeDepth;
    }
    scopes.exitScope();
}

bool SemanticAnalyzer::enterNamedBorrowScope(const std::string& name, const SourceSpan& span, bool isFormalParameter) {
    if (lookupNamedBorrowScope(name)) {
        reportError(span, "Borrow scope '" + name + "' is already active. Choose a different scope name.");
        return false;
    }

    namedBorrowScopes.push_back(NamedBorrowScope{name, span, lexicalScopeDepth, isFormalParameter});
    return true;
}

void SemanticAnalyzer::exitNamedBorrowScope() {
    if (!namedBorrowScopes.empty()) {
        namedBorrowScopes.pop_back();
    }
}

const NamedBorrowScope* SemanticAnalyzer::lookupNamedBorrowScope(const std::string& name) const {
    for (auto it = namedBorrowScopes.rbegin(); it != namedBorrowScopes.rend(); ++it) {
        if (it->name == name) {
            return &*it;
        }
    }
    return nullptr;
}

bool SemanticAnalyzer::validateScopedViewType(const ResolvedType& type, const AstNode* node, std::string_view context) {
    if (!type.isView() || type.viewScope.empty()) {
        return true;
    }

    if (type.viewKind != "look") {
        reportError(node, "Scoped borrows currently use `ref[s]`, not `" + type.describe() + "`.");
        return false;
    }

    if (!lookupNamedBorrowScope(type.viewScope)) {
        reportError(
            node,
            "Scoped borrow `" + type.describe() + "` is only valid inside `scope " + type.viewScope +
                " { ... }` for " + std::string(context) + ".");
        return false;
    }

    return true;
}

bool SemanticAnalyzer::validateAnchorPayloadType(const ResolvedType& type, const AstNode* node, std::string_view context) {
    std::unordered_set<std::string> visitedTypes;
    auto validateNested = [&](const auto& self, const ResolvedType& current, bool insideAnchorPayload) -> bool {
        if (current.isUnknown()) {
            return true;
        }
        if (canonicalTypeName(current.name) == "Anchor") {
            if (insideAnchorPayload) {
                reportError(node, "`" + type.describe() + "` is unsupported because an Anchor payload cannot contain another Anchor (found nested type `" + current.describe() + "`); dropping the outer Anchor only frees its own storage. Recursive payload destruction is not supported.");
                return false;
            }
            if (current.params.size() != 1) {
                reportError(node, "Anchor[T] requires exactly one payload type.");
                return false;
            }
            return self(self, current.params.front(), true);
        }
        if (insideAnchorPayload) {
            if (current.isOpaqueExternal()) {
                reportError(node, "`" + type.describe() + "` cannot be used in " + std::string(context) +
                    " because Anchor[T] requires a concrete owned payload, not an opaque external value.");
                return false;
            }
            if (current.isView()) {
                reportError(node, "`" + type.describe() + "` cannot be used in " + std::string(context) +
                    " because Anchor[T] requires an owned payload. Store an owned value instead of `" + current.describe() + "`.");
                return false;
            }
            if (!current.scopeName.empty() || !current.viewScope.empty()) {
                const std::string scopeName = !current.scopeName.empty() ? current.scopeName : current.viewScope;
                reportError(node, "`" + type.describe() + "` cannot be used in " + std::string(context) +
                    " because Anchor[T] cannot hold scoped type `" + current.describe() +
                    "` bound to scope `" + scopeName + "`.");
                return false;
            }
            if (canonicalTypeName(current.name) == "Span") {
                reportError(node, "`" + type.describe() + "` cannot be used in " + std::string(context) +
                    " because Anchor[T] cannot hold Span[T]. Keep the Span borrow local or store the owned collection instead.");
                return false;
            }
            if (isRawAddressType(current)) {
                reportError(node, "`" + type.describe() + "` cannot be used in " + std::string(context) +
                    " because Anchor[T] cannot hold raw address types.");
                return false;
            }
        }

        const std::string visitKey = current.nominalIdentity + "|" + current.describe() +
            (insideAnchorPayload ? "|anchor-payload" : "|ordinary-type");
        if (!visitedTypes.insert(visitKey).second) {
            return true;
        }

        for (const auto& param : current.params) {
            if (!self(self, param, insideAnchorPayload)) {
                return false;
            }
        }

        if (const ShapeInfo* shape = lookupShape(current.name)) {
            const auto bindings = buildTypeBindings(shape->typeParams, current.params);
            for (const auto& fieldName : shape->fieldOrder) {
                const auto fieldIt = shape->fields.find(fieldName);
                if (fieldIt == shape->fields.end()) {
                    continue;
                }
                const ResolvedType fieldType = substituteType(fieldIt->second, bindings);
                if (!self(self, fieldType, insideAnchorPayload)) {
                    return false;
                }
            }
        }

        if (const ChoiceInfo* choice = lookupChoice(current.name)) {
            const auto bindings = buildTypeBindings(choice->typeParams, current.params);
            for (const auto& variantName : choice->variantOrder) {
                const auto variantIt = choice->variants.find(variantName);
                if (variantIt == choice->variants.end()) {
                    continue;
                }
                for (const auto& payloadType : variantIt->second.payloadTypes) {
                    const ResolvedType specializedPayload = substituteType(payloadType, bindings);
                    if (!self(self, specializedPayload, insideAnchorPayload)) {
                        return false;
                    }
                }
            }
        }

        return true;
    };

    return validateNested(validateNested, type, false);
}
const Symbol* SemanticAnalyzer::resolveBorrowSourceSymbol(const Expr* expr) const {
    if (!expr) {
        return nullptr;
    }

    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        const auto sym = lookupSymbol(ident->name);
        return sym && sym->kind == SymbolKind::Variable ? sym.get() : nullptr;
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return resolveBorrowSourceSymbol(member->object.get());
    }

    if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return resolveBorrowSourceSymbol(index->object.get());
    }

    if (auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
        return resolveBorrowSourceSymbol(borrow->target.get());
    }

    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        const FunctionSignature* signature = lookupCallableSignature(call->callee.get());
        auto methodSignature = lookupMethodSignature(call->callee.get());

        if (methodSignature.has_value()) {
            if (methodSignature->viewReturnFromReceiver) {
                if (auto* member = dynamic_cast<const MemberExpr*>(call->callee.get())) {
                    return resolveBorrowSourceSymbol(member->object.get());
                }
                return nullptr;
            }
            if (methodSignature->viewReturnSourceArg.has_value()) {
                const size_t sourceIndex = *methodSignature->viewReturnSourceArg;
                if (sourceIndex < call->args.size()) {
                    return resolveBorrowSourceSymbol(call->args[sourceIndex].get());
                }
                return nullptr;
            }
        }

        if (signature &&
            (signature->returnType.isView() || isBorrowedStrMaybeType(signature->returnType)) &&
            signature->viewReturnSourceParam.has_value()) {
            const size_t sourceIndex = *signature->viewReturnSourceParam;
            if (sourceIndex < call->args.size()) {
                return resolveBorrowSourceSymbol(call->args[sourceIndex].get());
            }
        }
        if (signature && typeContainsScopeName(signature->returnType) && signature->scopeReturnSourceParam.has_value()) {
            const size_t sourceIndex = *signature->scopeReturnSourceParam;
            if (sourceIndex < call->args.size()) {
                return resolveBorrowSourceSymbol(call->args[sourceIndex].get());
            }
        }
    }

    return nullptr;
}

bool SemanticAnalyzer::validateScopedBorrowSource(
    const Expr* expr,
    const ResolvedType& targetType,
    const AstNode* node,
    std::string_view context) {
    if (!targetType.isView() || targetType.viewScope.empty()) {
        return true;
    }

    const auto* namedScope = lookupNamedBorrowScope(targetType.viewScope);
    if (!namedScope) {
        reportError(
            node,
            "Scoped borrow `" + targetType.describe() + "` is only valid inside `scope " + targetType.viewScope +
                " { ... }` for " + std::string(context) + ".");
        return false;
    }

    const auto* sourceSymbol = resolveBorrowSourceSymbol(expr);
    if (!sourceSymbol) {
        reportError(
            node,
            "Scoped borrow `" + targetType.describe() +
                "` must come from a named value that lives for the whole scope. Borrow a stable binding instead.");
        return false;
    }

    if (sourceSymbol->lexicalDepth > namedScope->lexicalDepth) {
        reportError(
            node,
            "Source value for `" + targetType.describe() + "` does not live long enough for scope `" +
                targetType.viewScope + "`. Move the source binding outside the inner block or keep the ref in a shorter scope.");
        return false;
    }

    if (namedScope->isFormalParameter) {
        std::vector<std::string> sourceScopes;
        collectNamedScopeNames(sourceSymbol->type, sourceScopes);
        if (std::find(sourceScopes.begin(), sourceScopes.end(), targetType.viewScope) == sourceScopes.end()) {
            reportError(
                node,
                "Source value for `" + targetType.describe() + "` is not tied to scope parameter '" +
                    targetType.viewScope + "'. Borrow from a parameter that carries the same scope.");
            return false;
        }
    }

    if (sourceSymbol->type.isView() && !sourceSymbol->type.viewScope.empty() &&
        sourceSymbol->type.viewScope != targetType.viewScope) {
        reportError(
            node,
            "Cannot rebind `" + sourceSymbol->type.describe() + "` into `" + targetType.describe() +
                "`. Scoped refs must stay in the same named scope.");
        return false;
    }

    return true;
}

void SemanticAnalyzer::registerPrelude() {
    auto registerBuiltin = [&](const std::string& name) {
        FunctionSignature signature;
        signature.paramTypes.push_back(makeUnknownType());
        signature.returnType = makePlainType("Unit");
        signature.isExternal = true;

        analysisResult.functionsByName[name] = signature;

        auto sym = std::make_shared<Symbol>();
        sym->name = name;
        sym->kind = SymbolKind::Function;
        sym->isExternal = true;
        sym->type = makeUnknownType(name);
        if (!scopes.define(name, sym)) {
            reportError("Duplicate prelude name: " + name);
        }
    };

    registerBuiltin("print");
    registerBuiltin("println");
}

void SemanticAnalyzer::analyze(RealmDecl* realm) {
    analysisResult = AnalysisResult{};
    diagnostics.clear();
    currentFunction = nullptr;
    currentSignature = nullptr;
    implicitTailExpr = nullptr;
    currentViewReturnSourceParam.reset();
    currentViewReturnSeen = false;
    loopDepth = 0;
    rawDepth = 0;
    activeNamedScopes.clear();
    lexicalScopeDepth = 0;
    namedBorrowScopes.clear();

    // Inject built-in choices if not already declared by the user
    bool hasMaybe = false;
    bool hasResult = false;
    for (const auto& decl : realm->declarations) {
        if (auto* choice = dynamic_cast<ChoiceDecl*>(decl.get())) {
            if (choice->name == "Maybe") hasMaybe = true;
            if (choice->name == "Result") hasResult = true;
        }
    }

    if (!hasMaybe) {
        auto maybeChoice = std::make_unique<ChoiceDecl>();
        maybeChoice->name = "Maybe";
        maybeChoice->typeParams = {"T"};
        ChoiceVariant noneVar;
        noneVar.tag = "None";
        ChoiceVariant someVar;
        someVar.tag = "Some";
        FnParam somePayload;
        somePayload.name = "value";
        somePayload.type = std::make_unique<TypeNode>();
        somePayload.type->name = "T";
        someVar.payloads.push_back(std::move(somePayload));
        maybeChoice->variants.push_back(std::move(noneVar));
        maybeChoice->variants.push_back(std::move(someVar));
        realm->declarations.insert(realm->declarations.begin(), std::move(maybeChoice));
    }

    if (!hasResult) {
        auto resultChoice = std::make_unique<ChoiceDecl>();
        resultChoice->name = "Result";
        resultChoice->typeParams = {"T", "E"};
        ChoiceVariant okVar;
        okVar.tag = "Ok";
        FnParam okPayload;
        okPayload.name = "value";
        okPayload.type = std::make_unique<TypeNode>();
        okPayload.type->name = "T";
        okVar.payloads.push_back(std::move(okPayload));
        ChoiceVariant failVar;
        failVar.tag = "Fail";
        FnParam failPayload;
        failPayload.name = "cause";
        failPayload.type = std::make_unique<TypeNode>();
        failPayload.type->name = "E";
        failVar.payloads.push_back(std::move(failPayload));
        resultChoice->variants.push_back(std::move(okVar));
        resultChoice->variants.push_back(std::move(failVar));
        realm->declarations.insert(realm->declarations.begin(), std::move(resultChoice));
    }

    validateNameCasing(realm);

    enterSemanticScope();
    registerPrelude();
    registerImports(realm);
    declareTopLevel(realm);
    resolveTopLevelTypes(realm);
    validateNamedLayouts(realm);

    for (auto& decl : realm->declarations) {
        analyzeDecl(decl.get());
    }

    exitSemanticScope();

    // Preserve symbolic literal types while contexts are inferred. Only after all
    // declarations have been analyzed may uncontextualized floats default to
    // Float64. Finalize every analyzed float leaf, including unary operands,
    // discarded values and inferred aggregate/carrier payloads, before lowering.
    if (diagnostics.empty()) {
        std::vector<const FloatExpr*> floatLiterals;
        for (auto& [expr, type] : analysisResult.exprTypes) {
            if (isFloatLiteralType(type)) {
                type = defaultFloatLiteralType();
            }
            if (const auto* literal = dynamic_cast<const FloatExpr*>(expr)) {
                floatLiterals.push_back(literal);
            }
        }
        std::sort(floatLiterals.begin(), floatLiterals.end(), [](const FloatExpr* a, const FloatExpr* b) {
            if (a->span.line != b->span.line) return a->span.line < b->span.line;
            return a->span.column < b->span.column;
        });
        for (const auto* expr : floatLiterals) {
            const auto& type = analysisResult.exprTypes.at(expr);
            const auto literal = splitNumericLiteralText(expr->value);
            if (isFloatTypeName(type.name) && !floatLiteralFitsTarget(literal.magnitude, type.name)) {
                reportError(expr, "Float literal '" + literal.magnitude + "' does not fit target type " + type.name + ".");
            }
        }
    }

    if (!diagnostics.empty()) {
        throw DiagnosticError(
            "Semantic analysis failed with " + std::to_string(diagnostics.size()) + " errors.",
            diagnostics);
    }
}

void SemanticAnalyzer::validateNameCasing(const RealmDecl* realm) {
    const auto requireCase = [this](
                                const std::string& name,
                                const SourceSpan& span,
                                bool requireUppercase,
                                std::string_view category,
                                bool allowDiscard = false) {
        if (allowDiscard && name == "_") {
            return;
        }

        const bool valid = requireUppercase ? startsWithUppercase(name) : startsWithLowercase(name);
        if (!valid) {
            reportError(
                span,
                std::string(category) + " '" + name + "' must start with " +
                    (requireUppercase ? "an uppercase" : "a lowercase") + " letter.");
        }
    };

    const auto requireTypeParameters = [&](const std::vector<std::string>& names, const SourceSpan& span) {
        for (const auto& name : names) {
            requireCase(name, span, true, "Type parameter");
        }
    };

    std::function<void(const Expr*)> requireExpr;
    std::function<void(const PickPattern*)> requireTuplePattern;
    std::function<void(const BlockStmt*)> requireBlock;
    requireTuplePattern = [&](const PickPattern* pattern) {
        if (!pattern) {
            return;
        }
        if (pattern->kind == PickPatternKind::Binding) {
            requireCase(pattern->name, pattern->span, false, "Variable name", true);
            return;
        }
        for (const auto& element : pattern->elements) {
            requireTuplePattern(element.get());
        }
    };
    requireBlock = [&](const BlockStmt* block) {
        if (!block) {
            return;
        }

        for (const auto& stmt : block->statements) {
            if (const auto* binding = dynamic_cast<const BindingStmt*>(stmt.get())) {
                if (!binding->tuplePattern) {
                    requireCase(binding->name, binding->span, false, "Variable name", true);
                } else {
                    requireTuplePattern(binding->tuplePattern.get());
                }
                requireExpr(binding->value.get());
            } else if (const auto* tryStmt = dynamic_cast<const TryStmt*>(stmt.get())) {
                requireCase(tryStmt->name, tryStmt->span, false, "Variable name", true);
                if (!tryStmt->failName.empty()) {
                    requireCase(tryStmt->failName, tryStmt->span, false, "Variable name", true);
                }
                requireExpr(tryStmt->expr.get());
                requireBlock(tryStmt->failBlock.get());
            } else if (const auto* ifStmt = dynamic_cast<const IfStmt*>(stmt.get())) {
                requireExpr(ifStmt->condition.get());
                requireBlock(ifStmt->thenBlock.get());
                requireBlock(ifStmt->elseBlock.get());
            } else if (const auto* loop = dynamic_cast<const LoopStmt*>(stmt.get())) {
                requireExpr(loop->condition.get());
                requireBlock(loop->body.get());
            } else if (const auto* scan = dynamic_cast<const ScanStmt*>(stmt.get())) {
                requireCase(scan->itemName, scan->span, false, "Variable name", true);
                requireExpr(scan->iterable.get());
                requireBlock(scan->body.get());
            } else if (const auto* scope = dynamic_cast<const ScopeStmt*>(stmt.get())) {
                requireCase(scope->name, scope->span, false, "Scope name");
                requireBlock(scope->body.get());
            } else if (const auto* pick = dynamic_cast<const PickStmt*>(stmt.get())) {
                requireExpr(pick->value.get());
                for (const auto& branch : pick->branches) {
                    for (const auto& name : branch.bindings) {
                        requireCase(name, branch.span, false, "Variable name", true);
                    }
                    requireBlock(branch.body.get());
                }
            } else if (const auto* raw = dynamic_cast<const RawStmt*>(stmt.get())) {
                requireBlock(raw->body.get());
            } else if (const auto* exprStmt = dynamic_cast<const ExprStmt*>(stmt.get())) {
                requireExpr(exprStmt->expr.get());
            } else if (const auto* ret = dynamic_cast<const ReturnStmt*>(stmt.get())) {
                requireExpr(ret->value.get());
            } else if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt.get())) {
                requireExpr(assign->target.get());
                requireExpr(assign->value.get());
            }
        }
    };
    requireExpr = [&](const Expr* expr) {
        if (!expr) {
            return;
        }
        if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
            requireExpr(binary->left.get());
            requireExpr(binary->right.get());
        } else if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
            requireExpr(unary->operand.get());
        } else if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
            requireExpr(call->callee.get());
            for (const auto& arg : call->args) requireExpr(arg.get());
        } else if (const auto* member = dynamic_cast<const MemberExpr*>(expr)) {
            requireExpr(member->object.get());
        } else if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
            requireExpr(index->object.get());
            requireExpr(index->index.get());
        } else if (const auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
            requireExpr(borrow->target.get());
        } else if (const auto* shape = dynamic_cast<const ShapeInitExpr*>(expr)) {
            for (const auto& field : shape->fields) requireExpr(field.value.get());
        } else if (const auto* ifExpr = dynamic_cast<const IfExpr*>(expr)) {
            requireExpr(ifExpr->condition.get());
            requireBlock(ifExpr->thenBlock.get());
            requireBlock(ifExpr->elseBlock.get());
        } else if (const auto* pickExpr = dynamic_cast<const PickExpr*>(expr)) {
            requireExpr(pickExpr->value.get());
            for (const auto& branch : pickExpr->branches) {
                for (const auto& name : branch.bindings) {
                    requireCase(name, branch.span, false, "Variable name", true);
                }
                requireBlock(branch.body.get());
            }
        }
    };

    const auto requireFunction = [&](const FnDecl* fn) {
        if (!fn) {
            return;
        }
        requireCase(fn->name, fn->span, false, "Function name");
        for (const auto& param : fn->params) {
            requireCase(param.name, param.span, false, "Parameter name", true);
        }
        requireBlock(fn->body.get());
    };

    for (const auto& decl : realm->declarations) {
        if (const auto* shape = dynamic_cast<const ShapeDecl*>(decl.get())) {
            requireCase(shape->name, shape->span, true, "Type name");
            requireTypeParameters(shape->typeParams, shape->span);
            if (shape->isViewShape) {
                requireCase(shape->scopeParamName, shape->span, false, "Scope name");
            }
            for (const auto& field : shape->fields) {
                requireCase(field.name, field.span, false, "Field name");
            }
        } else if (const auto* choice = dynamic_cast<const ChoiceDecl*>(decl.get())) {
            requireCase(choice->name, choice->span, true, "Type name");
            requireTypeParameters(choice->typeParams, choice->span);
            for (const auto& variant : choice->variants) {
                for (const auto& payload : variant.payloads) {
                    requireCase(payload.name, payload.span, false, "Field name", true);
                }
            }
        } else if (const auto* contract = dynamic_cast<const ContractDecl*>(decl.get())) {
            requireCase(contract->name, contract->span, true, "Type name");
            requireTypeParameters(contract->typeParams, contract->span);
            for (const auto& method : contract->methods) {
                requireFunction(method.get());
            }
        } else if (const auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            requireFunction(fn);
        } else if (const auto* stat = dynamic_cast<const StaticDecl*>(decl.get())) {
            requireCase(stat->name, stat->span, false, "Variable name");
        } else if (const auto* impl = dynamic_cast<const ImplementsDecl*>(decl.get())) {
            requireCase(impl->typeName, impl->span, true, "Type name");
            if (impl->contractName.has_value()) {
                requireCase(*impl->contractName, impl->span, true, "Type name");
            }
            requireTypeParameters(impl->typeParams, impl->span);
            for (const auto& method : impl->methods) {
                requireFunction(method.get());
            }
        }
    }
}

void SemanticAnalyzer::registerImports(const RealmDecl* realm) {
    auto defineModule = [&](const std::string& name, std::shared_ptr<ModuleInfo> moduleInfo = {}) {
        auto sym = std::make_shared<Symbol>();
        sym->name = name;
        sym->kind = SymbolKind::Module;
        sym->isExternal = true;
        sym->type = makeUnknownType(name);
        sym->moduleInfo = std::move(moduleInfo);
        if (!scopes.define(name, sym)) {
            reportError("Duplicate imported name: " + name);
        }
    };

    auto defineFunction = [&](const ImportedBinding& binding) {
        if (binding.functionSignature.has_value()) {
            FunctionSignature signature = *binding.functionSignature;
            signature.isExternal = true;
            analysisResult.functionsByName[binding.name] = signature;
        }

        auto sym = std::make_shared<Symbol>();
        sym->name = binding.name;
        sym->kind = SymbolKind::Function;
        sym->isExternal = true;
        sym->type = makeUnknownType(binding.name);
        if (!scopes.define(binding.name, sym)) {
            reportError("Duplicate imported name: " + binding.name);
        }
    };

    auto defineType = [&](const ImportedBinding& binding) {
        if (binding.kind == SymbolKind::Choice) {
            typeCatalog.registerChoiceName(
                binding.name,
                binding.choiceInfo.has_value()
                    ? std::optional<size_t>(binding.choiceInfo->typeParams.size())
                    : std::nullopt);
            if (binding.choiceInfo.has_value()) {
                analysisResult.choicesByName[binding.name] = *binding.choiceInfo;
            }
        } else {
            if (binding.shapeInfo.has_value() && binding.shapeInfo->isViewShape) {
                typeCatalog.registerViewShapeName(binding.name, binding.shapeInfo->scopeParamName);
            } else {
                typeCatalog.registerShapeName(
                    binding.name,
                    binding.shapeInfo.has_value()
                        ? std::optional<size_t>(binding.shapeInfo->typeParams.size())
                        : std::nullopt);
            }
            if (binding.shapeInfo.has_value()) {
                analysisResult.shapesByName[binding.name] = *binding.shapeInfo;
            }
        }
        typeCatalog.registerNominalIdentity(binding.name, binding.nominalIdentity);

        auto sym = std::make_shared<Symbol>();
        sym->name = binding.name;
        sym->kind = binding.kind;
        sym->isExternal = true;
        sym->type = makeOwnedType(binding.name);
        if (!scopes.define(binding.name, sym)) {
            reportError("Duplicate imported name: " + binding.name);
        }
    };

    if (!importedBindings.empty()) {
        for (const auto& binding : importedBindings) {
            switch (binding.kind) {
                case SymbolKind::Module:
                    defineModule(binding.name, binding.moduleInfo);
                    break;
                case SymbolKind::Function:
                    defineFunction(binding);
                    break;
                case SymbolKind::Choice:
                case SymbolKind::Shape:
                    defineType(binding);
                    break;
                default:
                    defineModule(binding.name, binding.moduleInfo);
                    break;
            }
        }
        return;
    }

    auto registerImportedName = [&](const std::string& name) {
        if (startsWithUppercase(name)) {
            typeCatalog.registerShapeName(name);
        } else {
            defineModule(name);
        }
    };

    for (const auto& imp : realm->imports) {
        if (!imp.items.empty()) {
            for (const auto& item : imp.items) {
                registerImportedName(item.alias.empty() ? item.name : item.alias);
            }
            continue;
        }

        std::string importedName = imp.modulePath;
        const auto dotPos = importedName.rfind('.');
        if (dotPos != std::string::npos) {
            importedName = importedName.substr(dotPos + 1);
        }
        registerImportedName(importedName);
    }
}
void SemanticAnalyzer::declareTopLevel(const RealmDecl* realm) {
    for (const auto& decl : realm->declarations) {
        if (auto* shape = dynamic_cast<ShapeDecl*>(decl.get())) {
            if (shape->isViewShape) {
                typeCatalog.registerViewShapeName(shape->name, shape->scopeParamName);
            } else {
                typeCatalog.registerShapeName(shape->name, shape->typeParams.size());
            }
            typeCatalog.registerNominalIdentity(shape->name, nominalTypeIdentity(realm->name, shape->name));
        } else if (auto* choice = dynamic_cast<ChoiceDecl*>(decl.get())) {
            typeCatalog.registerChoiceName(choice->name, choice->typeParams.size());
            typeCatalog.registerNominalIdentity(choice->name, nominalTypeIdentity(realm->name, choice->name));
        }
    }

    for (const auto& decl : realm->declarations) {
        auto sym = std::make_shared<Symbol>();

        if (auto* fn = dynamic_cast<FnDecl*>(decl.get())) {
            sym->name = fn->name;
            sym->kind = SymbolKind::Function;
            sym->type = makeUnknownType(fn->name);
            if (!scopes.define(fn->name, sym)) {
                reportError(fn, "Duplicate declaration: " + fn->name);
            }
        } else if (auto* shape = dynamic_cast<ShapeDecl*>(decl.get())) {
            sym->name = shape->name;
            sym->kind = SymbolKind::Shape;
            sym->type = makeOwnedType(shape->name);
            if (!scopes.define(shape->name, sym)) {
                reportError(shape, "Duplicate type declaration: " + shape->name);
            }
        } else if (auto* choice = dynamic_cast<ChoiceDecl*>(decl.get())) {
            sym->name = choice->name;
            sym->kind = SymbolKind::Choice;
            sym->type = makeOwnedType(choice->name);
            if (!scopes.define(choice->name, sym)) {
                reportError(choice, "Duplicate type declaration: " + choice->name);
            }
        } else if (auto* stat = dynamic_cast<StaticDecl*>(decl.get())) {
            sym->name = stat->name;
            sym->kind = SymbolKind::Variable;
            sym->isStatic = true;
            sym->type = makeUnknownType(stat->name);
            if (!scopes.define(stat->name, sym)) {
                reportError(stat, "Duplicate static declaration: " + stat->name);
            }
        } else if (auto* contract = dynamic_cast<ContractDecl*>(decl.get())) {
            sym->name = contract->name;
            sym->kind = SymbolKind::Contract;
            sym->type = makePlainType(contract->name);
            if (!scopes.define(contract->name, sym)) {
                reportError(contract, "Duplicate contract declaration: " + contract->name);
            }
        }
    }
}

void SemanticAnalyzer::resolveTopLevelTypes(const RealmDecl* realm) {
    for (const auto& decl : realm->declarations) {
        if (auto* shape = dynamic_cast<ShapeDecl*>(decl.get())) {
            analyzeShapeDecl(shape);
        } else if (auto* choice = dynamic_cast<ChoiceDecl*>(decl.get())) {
            analyzeChoiceDecl(choice);
        } else if (auto* stat = dynamic_cast<StaticDecl*>(decl.get())) {
            auto sym = lookupSymbol(stat->name);
            if (sym) {
                sym->type = resolveTypeNode(stat->type.get(), {});
                if (sym->type.name == "Array") {
                    reportError(stat, "Global fixed Array values are out of scope; Array is supported only as a local or function value.");
                }
            }
            if (stat->init) {
                const ResolvedType initType = analyzeExpr(stat->init.get(), sym ? &sym->type : nullptr);
                if (sym && !canAssignType(initType, sym->type)) {
                    reportError(stat->init.get(), "Static initializer type mismatch: expected " + sym->type.describe() + ", got " + initType.describe());
                }
                bool isLiteral = dynamic_cast<IntExpr*>(stat->init.get()) != nullptr ||
                                 dynamic_cast<FloatExpr*>(stat->init.get()) != nullptr ||
                                 dynamic_cast<StringExpr*>(stat->init.get()) != nullptr ||
                                 dynamic_cast<CharExpr*>(stat->init.get()) != nullptr ||
                                 dynamic_cast<BoolExpr*>(stat->init.get()) != nullptr;
                if (!isLiteral) {
                    reportError(stat->init.get(), "Static variable initializer must be a compile-time literal expression.");
                }
            } else {
                reportError(stat, "Static variable requires an initializer.");
            }
        } else if (auto* contract = dynamic_cast<ContractDecl*>(decl.get())) {
            ContractInfo info;
            info.name = contract->name;
            info.typeParams = contract->typeParams;
            for (const auto& method : contract->methods) {
                FunctionSignature sig;
                std::unordered_set<std::string> typeParams;
                for (const auto& param : contract->typeParams) {
                    typeParams.insert(param);
                }
                for (const auto& param : method->params) {
                    if (param.isSelf) {
                        std::string vk = param.type ? param.type->viewKind : "";
                        ResolvedType selfType;
                        if (!vk.empty()) {
                            selfType = asViewType(makeOwnedType("Self"), vk);
                        } else {
                            selfType = makeOwnedType("Self");
                        }
                        sig.receiverType = selfType;
                    } else {
                        sig.paramTypes.push_back(resolveTypeNode(param.type.get(), typeParams));
                    }
                }
                sig.returnType = method->returnType ? resolveTypeNode(method->returnType.get(), typeParams) : makePlainType("Unit");
                populateFunctionScopeMetadata(sig);
                info.methods[method->name] = sig;
                // Also register contract method signature for type-checking
                analysisResult.contractMethods[contract->name + "." + method->name] = sig;
            }
            analysisResult.contractsByName[contract->name] = info;
        }
    }

    for (const auto& decl : realm->declarations) {
        if (auto* fn = dynamic_cast<FnDecl*>(decl.get())) {
            FunctionSignature signature;
            std::unordered_set<std::string> typeParams;

            for (const auto& param : fn->params) {
                signature.paramTypes.push_back(resolveTypeNode(param.type.get(), typeParams));
            }

            signature.returnType = fn->returnType
                ? resolveTypeNode(fn->returnType.get(), typeParams)
                : makePlainType("Unit");
            populateFunctionScopeMetadata(signature);

            analysisResult.functionSignatures[fn] = signature;
            analysisResult.functionsByName[fn->name] = signature;
        } else if (auto* impl = dynamic_cast<ImplementsDecl*>(decl.get())) {
            std::unordered_map<std::string, FunctionSignature> implMethodSignatures;
            for (const auto& method : impl->methods) {
                FunctionSignature signature;
                std::unordered_set<std::string> typeParams;

                for (const auto& param : method->params) {
                    if (param.isSelf) {
                        std::string vk = param.type ? param.type->viewKind : "";
                        ResolvedType selfType;
                        if (!vk.empty()) {
                            selfType = asViewType(makeOwnedType(impl->typeName), vk);
                        } else {
                            selfType = makeOwnedType(impl->typeName);
                        }
                        signature.receiverType = selfType;
                    } else {
                        signature.paramTypes.push_back(resolveTypeNode(param.type.get(), typeParams));
                    }
                }

                signature.returnType = method->returnType
                    ? resolveTypeNode(method->returnType.get(), typeParams)
                    : makePlainType("Unit");
                populateFunctionScopeMetadata(signature);

                std::string fullMethodName = impl->typeName + "." + method->name;
                analysisResult.functionSignatures[method.get()] = signature;
                analysisResult.functionsByName[fullMethodName] = signature;
                implMethodSignatures[method->name] = signature;
            }

            if (impl->contractName.has_value()) {
                validateContractImplementation(*impl, implMethodSignatures);
                analysisResult.shapeContracts[impl->typeName].push_back(*impl->contractName);
            }
        }
    }
}

void SemanticAnalyzer::validateContractImplementation(
    const ImplementsDecl& impl,
    const std::unordered_map<std::string, FunctionSignature>& implMethods) {
    const std::string& contractName = *impl.contractName;

    const auto contractIt = analysisResult.contractsByName.find(contractName);
    if (contractIt == analysisResult.contractsByName.end()) {
        reportError(&impl, "Unknown contract: " + contractName);
        return;
    }

    // Reject implementing the same contract twice for the same shape.
    const auto& existing = analysisResult.shapeContracts[impl.typeName];
    if (std::find(existing.begin(), existing.end(), contractName) != existing.end()) {
        reportError(&impl, "Contract '" + contractName + "' is already implemented for '" + impl.typeName + "'.");
        return;
    }

    const ContractInfo& contract = contractIt->second;
    for (const auto& [methodName, contractSig] : contract.methods) {
        const auto implIt = implMethods.find(methodName);
        if (implIt == implMethods.end()) {
            reportError(&impl, "Contract '" + contractName + "' is not fully implemented: missing method '" + methodName + "'.");
            continue;
        }

        const FunctionSignature& implSig = implIt->second;

        // `Self` in the contract resolves to the implementing type.
        const auto substituteSelf = [&](const ResolvedType& t) {
            ResolvedType out = t;
            if (out.name == "Self") {
                out.name = impl.typeName;
            }
            return out;
        };

        if (contractSig.receiverType.has_value() != implSig.receiverType.has_value()) {
            reportError(&impl, "Method '" + methodName + "' receiver type does not match contract '" + contractName + "'.");
        } else if (contractSig.receiverType.has_value()) {
            const ResolvedType expectedReceiver = substituteSelf(contractSig.receiverType.value());
            if (expectedReceiver.isView() != implSig.receiverType->isView() || 
                expectedReceiver.viewKind != implSig.receiverType->viewKind) {
                reportError(&impl, "Method '" + methodName + "' receiver type does not match contract '" + contractName + "': expected " + expectedReceiver.describe() + ", got " + implSig.receiverType->describe() + ".");
            }
        }

        if (contractSig.paramTypes.size() != implSig.paramTypes.size()) {
            reportError(&impl, "Method '" + methodName + "' does not match contract '" + contractName +
                                 "': expected " + std::to_string(contractSig.paramTypes.size()) +
                                 " parameter(s), got " + std::to_string(implSig.paramTypes.size()) + ".");
            continue;
        }

        for (size_t i = 0; i < contractSig.paramTypes.size(); ++i) {
            const ResolvedType expected = substituteSelf(contractSig.paramTypes[i]);
            const ResolvedType actual = implSig.paramTypes[i];
            if (expected.describe() != actual.describe()) {
                reportError(&impl, "Method '" + methodName + "' parameter " + std::to_string(i) +
                                     " does not match contract '" + contractName + "': expected " +
                                     expected.describe() + ", got " + actual.describe() + ".");
            }
        }

        const ResolvedType expectedReturn = substituteSelf(contractSig.returnType);
        if (expectedReturn.describe() != implSig.returnType.describe()) {
            reportError(&impl, "Method '" + methodName + "' return type does not match contract '" +
                                 contractName + "': expected " + expectedReturn.describe() +
                                 ", got " + implSig.returnType.describe() + ".");
        }
    }
}


void SemanticAnalyzer::validateNamedLayouts(const RealmDecl* realm) {
    for (const auto& decl : realm->declarations) {
        if (auto* shape = dynamic_cast<ShapeDecl*>(decl.get())) {
            bool hadSpecificError = false;
            std::vector<std::string> stack{shape->name};
            const auto* info = lookupShape(shape->name);
            if (info) {
                for (const auto& field : shape->fields) {
                    const auto fieldIt = info->fields.find(field.name);
                    if (fieldIt == info->fields.end()) {
                        continue;
                    }
                    if (containsOwnedTupleValue(fieldIt->second)) {
                        reportError(
                            field.span,
                            "Shape fields cannot contain tuples with owned fields until shape drop glue is available.");
                        hadSpecificError = true;
                    }
                    hadSpecificError = validateOwnedLayoutDependency(shape->name, fieldIt->second, field.span, stack) || hadSpecificError;
                }
            }

            ResolvedType type = makeOwnedType(shape->name);
            const auto layout = computeTypeLayout(type, analysisResult, target);
            if (!layout.has_value() && !hadSpecificError) {
                reportError(shape, "Type layout could not be resolved for '" + shape->name + "'. Use indirection or remove recursive owned fields.");
            }
        } else if (auto* choice = dynamic_cast<ChoiceDecl*>(decl.get())) {
            bool hadSpecificError = false;
            std::vector<std::string> stack{choice->name};
            const auto* info = lookupChoice(choice->name);
            if (info) {
                for (const auto& variant : choice->variants) {
                    const auto variantIt = info->variants.find(variant.tag);
                    if (variantIt == info->variants.end()) {
                        continue;
                    }
                    for (size_t i = 0; i < variant.payloads.size() && i < variantIt->second.payloadTypes.size(); ++i) {
                        hadSpecificError = validateOwnedLayoutDependency(choice->name, variantIt->second.payloadTypes[i], variant.payloads[i].span, stack) || hadSpecificError;
                    }
                }
            }

            ResolvedType type = makeOwnedType(choice->name);
            const auto layout = computeTypeLayout(type, analysisResult, target);
            if (!layout.has_value() && !hadSpecificError) {
                reportError(choice, "Type layout could not be resolved for '" + choice->name + "'. Use indirection or remove recursive owned payloads.");
            }
        }
    }
}

bool SemanticAnalyzer::validateOwnedLayoutDependency(
    const std::string& rootName,
    const ResolvedType& type,
    const SourceSpan& span,
    std::vector<std::string>& stack) {
    if (type.isUnknown() || type.isOpaqueExternal() || type.isPlain() || type.isView() || type.isGeneric) {
        return false;
    }
    if (type.name == "Str" || type.name == "Span" || type.name == "CStr" ||
        type.name == "OwnedCStr" || type.name == "Vec" || type.name == "Map" || type.name == "Set" ||
        type.name == "Queue" || type.name == "Arena" || type.name == "Anchor" ||
        type.name == "Addr" || type.name == "RawPtr" || type.name == "RawMut" ||
        type.name == "Fn") {
        return false;
    }

    const auto stackIt = std::find(stack.begin(), stack.end(), type.name);
    if (stackIt != stack.end()) {
        std::string cycle;
        for (auto it = stackIt; it != stack.end(); ++it) {
            if (!cycle.empty()) {
                cycle += " -> ";
            }
            cycle += *it;
        }
        cycle += " -> " + type.name;
        reportError(span, "Owned type layout cycle requires indirection: " + cycle + ".");
        return true;
    }

    const auto* shape = lookupShape(type.name);
    const auto* choice = lookupChoice(type.name);
    if (!shape && !choice) {
        return false;
    }

    stack.push_back(type.name);
    bool hadError = false;

    if (shape) {
        for (const auto& fieldName : shape->fieldOrder) {
            const auto fieldIt = shape->fields.find(fieldName);
            if (fieldIt == shape->fields.end()) {
                continue;
            }
            const ResolvedType fieldType = instantiateGenericType(
                fieldIt->second,
                shape->typeParams,
                type.params,
                "shape '" + type.name + "'");
            hadError = validateOwnedLayoutDependency(rootName, fieldType, span, stack) || hadError;
        }
    }

    if (choice) {
        for (const auto& variantName : choice->variantOrder) {
            const auto variantIt = choice->variants.find(variantName);
            if (variantIt == choice->variants.end()) {
                continue;
            }
            for (const auto& payloadType : variantIt->second.payloadTypes) {
                const ResolvedType instantiatedPayload = instantiateGenericType(
                    payloadType,
                    choice->typeParams,
                    type.params,
                    "choice '" + type.name + "'");
                hadError = validateOwnedLayoutDependency(rootName, instantiatedPayload, span, stack) || hadError;
            }
        }
    }

    stack.pop_back();
    return hadError;
}

ResolvedType SemanticAnalyzer::resolveTypeNode(
    const TypeNode* node,
    const std::unordered_set<std::string>& localTypeParams) {
    if (node && node->name == "Self") {
        ResolvedType type = makePlainType("Self");
        if (!node->viewKind.empty()) {
            type = asViewType(type, node->viewKind);
        }
        return type;
    }
    ResolvedType type = typeCatalog.resolveType(node, localTypeParams, &diagnostics, target);
    if (node) {
        validateAnchorPayloadType(type, node, "type positions");
    }
    return type;
}

std::unordered_map<std::string, ResolvedType> SemanticAnalyzer::buildTypeBindingsChecked(
    const std::vector<std::string>& paramNames,
    const std::vector<ResolvedType>& argTypes,
    const std::string& context) {
    if (paramNames.empty()) {
        return {};
    }

    if (paramNames.size() != argTypes.size()) {
        reportError(
            "Generic argument count mismatch for " + context + ": expected " +
            std::to_string(paramNames.size()) + ", got " + std::to_string(argTypes.size()));
    }

    return buildTypeBindings(paramNames, argTypes);
}

ResolvedType SemanticAnalyzer::instantiateGenericType(
    const ResolvedType& type,
    const std::vector<std::string>& paramNames,
    const std::vector<ResolvedType>& argTypes,
    const std::string& context) {
    const auto bindings = buildTypeBindingsChecked(paramNames, argTypes, context);
    return substituteType(type, bindings);
}

std::optional<MethodSignature> SemanticAnalyzer::lookupMethodSignature(
    const ResolvedType& receiverType,
    const std::string& methodName) const {
    if (receiverType.isUnknown() || receiverType.isOpaqueExternal()) {
        return std::nullopt;
    }

    if (receiverType.name == "Arena" && methodName == "alloc") {
        MethodSignature signature;
        signature.name = methodName;
        signature.receiverType = asViewType(receiverType, "look");
        signature.function.isExternal = true;
        signature.viewReturnFromReceiver = true;
        signature.isBuiltin = true;
        return signature;
    }
    
    if (receiverType.name == "Arena" && methodName == "reset") {
        MethodSignature signature;
        signature.name = methodName;
        signature.receiverType = asViewType(receiverType, "edit");
        signature.function.returnType = makePlainType("Unit");
        signature.function.isExternal = true;
        signature.isBuiltin = true;
        return signature;
    }

    if (receiverType.name == "Anchor" && methodName == "get" && receiverType.params.size() == 1) {
        MethodSignature signature;
        signature.name = methodName;
        signature.receiverType = asViewType(receiverType, "look");
        signature.function.returnType = asViewType(receiverType.params.front(), "look");
        signature.function.isExternal = true;
        signature.viewReturnFromReceiver = true;
        signature.isBuiltin = true;
        return signature;
    }

    if (methodName == "span" && (receiverType.name == "Vec" || receiverType.name == "Array") && receiverType.params.size() == 1) {
        MethodSignature signature;
        signature.name = methodName;
        signature.receiverType = asViewType(receiverType, "look");
        ResolvedType spanType = makeOwnedType("Span");
        spanType.params.push_back(receiverType.params.front());
        signature.function.returnType = std::move(spanType);
        signature.function.isExternal = true;
        signature.viewReturnFromReceiver = true;
        signature.isBuiltin = true;
        return signature;
    }

    for (const auto& spec : builtin_methods::builtinMethodSpecs()) {
        if (spec.receiverName != receiverType.name || spec.methodName != methodName) {
            continue;
        }

        MethodSignature signature;
        signature.name = spec.methodName;
        signature.receiverType = asViewType(receiverType, spec.receiverViewKind);
        signature.function.paramTypes = spec.paramTypes;
        signature.function.returnType = spec.returnType;
        signature.function.isExternal = true;
        signature.viewReturnSourceArg = spec.viewReturnSourceArg;
        signature.viewReturnFromReceiver = spec.viewReturnFromReceiver;
        signature.isBuiltin = true;
        return signature;
    }

    const std::string fullMethodName = receiverType.name + "." + methodName;
    const auto it = analysisResult.functionsByName.find(fullMethodName);
    if (it != analysisResult.functionsByName.end()) {
        if (!it->second.receiverType.has_value()) {
            return std::nullopt;
        }
        MethodSignature signature;
        signature.name = methodName;
        signature.receiverType = it->second.receiverType.value();
        signature.function = it->second;
        signature.isBuiltin = false;
        return signature;
    }

    return std::nullopt;
}

void SemanticAnalyzer::analyzeDecl(Decl* decl) {
    if (auto* fn = dynamic_cast<FnDecl*>(decl)) {
        analyzeFnDecl(fn);
    } else if (auto* impl = dynamic_cast<ImplementsDecl*>(decl)) {
        for (const auto& method : impl->methods) {
            analyzeFnDecl(method.get(), impl->typeName);
        }
    }
}

void SemanticAnalyzer::analyzeFnDecl(FnDecl* fn, const std::string& receiverTypeName) {
    enterSemanticScope();

    const FnDecl* previousFunction = currentFunction;
    const FunctionSignature* previousSignature = currentSignature;
    const ExprStmt* previousImplicitTailExpr = implicitTailExpr;
    const auto previousViewReturnSourceParam = currentViewReturnSourceParam;
    const bool previousViewReturnSeen = currentViewReturnSeen;
    const int previousRawDepth = rawDepth;
    const size_t previousNamedScopeCount = namedBorrowScopes.size();
    const size_t previousActiveNamedScopeCount = activeNamedScopes.size();
    currentFunction = fn;
    
    if (!receiverTypeName.empty()) {
        std::string fullMethodName = receiverTypeName + "." + fn->name;
        currentSignature = lookupFunctionSignature(fullMethodName);
    } else {
        currentSignature = lookupFunctionSignature(fn);
    }
    
    implicitTailExpr = nullptr;
    currentViewReturnSourceParam.reset();
    currentViewReturnSeen = false;
    rawDepth = 0;

    if (currentSignature) {
        if (currentSignature->returnType.viewKind == "edit") {
            reportError(fn, "Safe functions cannot return ref mut views. Return an owned value or a ref view derived from a parameter.");
        }
        std::vector<std::string> returnScopes;
        collectNamedScopeNames(currentSignature->returnType, returnScopes);
        std::vector<std::string> parameterScopes;
        for (const auto& paramType : currentSignature->paramTypes) {
            collectNamedScopeNames(paramType, parameterScopes);
        }
        for (const auto& scopeName : returnScopes) {
            if (std::find(parameterScopes.begin(), parameterScopes.end(), scopeName) == parameterScopes.end()) {
                reportError(
                    fn,
                    "Function '" + fn->name + "' return scope parameter '" + scopeName +
                        "' is not bound by any function parameter.");
            }
        }
        for (size_t i = 0; i < fn->params.size() && i < currentSignature->paramTypes.size(); ++i) {
            if (isRawAddressType(currentSignature->paramTypes[i])) {
                reportError(fn->params[i].span, "Raw address types are not allowed in safe function parameters.");
            }
        }
        if (isRawAddressType(currentSignature->returnType)) {
            reportError(fn, "Raw address types are not allowed in safe function return types.");
        }
        for (const auto& scopeName : currentSignature->scopeParams) {
            if (enterNamedBorrowScope(scopeName, fn->span, true)) {
                activeNamedScopes.push_back(scopeName);
            }
        }
    }

    size_t nonSelfParamIdx = 0;
    for (size_t i = 0; i < fn->params.size(); ++i) {
        ResolvedType type;
        if (fn->params[i].isSelf) {
            if (currentSignature && currentSignature->receiverType.has_value()) {
                type = currentSignature->receiverType.value();
            } else if (!receiverTypeName.empty()) {
                std::string vk = fn->params[i].type ? fn->params[i].type->viewKind : "";
                if (!vk.empty()) {
                    type = asViewType(makeOwnedType(receiverTypeName), vk);
                } else {
                    type = makeOwnedType(receiverTypeName);
                }
            } else {
                type = makeUnknownType();
            }
        } else {
            type = (currentSignature && nonSelfParamIdx < currentSignature->paramTypes.size())
                ? currentSignature->paramTypes[nonSelfParamIdx++]
                : makeUnknownType();
        }
        defineVariable(
            fn->params[i].name,
            type,
            false,
            "Duplicate parameter name: " + fn->params[i].name,
            fn->params[i].span,
            (type.isView() || type.name == "Span" || typeContainsScopeName(type) || isBorrowedStrMaybeType(type))
                ? std::optional<size_t>(i)
                : std::nullopt);
    }

    bool hasImplicitTailReturn = false;
    if (fn->body) {
        if (!fn->body->statements.empty()) {
            implicitTailExpr = dynamic_cast<ExprStmt*>(fn->body->statements.back().get());
        }
        analyzeBlock(fn->body.get());

        if (!fn->body->statements.empty()) {
            if (auto* tailExprStmt = dynamic_cast<ExprStmt*>(fn->body->statements.back().get())) {
                const ResolvedType valueType = tailExprStmt->expr
                    ? (lookupExprType(tailExprStmt->expr.get()) ? *lookupExprType(tailExprStmt->expr.get()) : makeUnknownType())
                    : makePlainType("Unit");
                hasImplicitTailReturn = valueType.name != "Never";

                if (valueType.name != "Never" && typeContainsBorrowedStorage(valueType) &&
                    tailExprStmt->expr && expressionReturnsBorrowFromOwnedTemporary(*this, tailExprStmt->expr.get())) {
                    reportError(
                        tailExprStmt->expr.get(),
                        "Borrowed view cannot escape a temporary owner. Hint: keep the owner in a local variable, then derive the view from it.");
                } else if (valueType.name != "Never" && valueType.isOpaqueExternal()) {
                    reportError(tailExprStmt->expr.get(), opaqueExternalValueUseMessage(valueType, "implicit return value"));
                } else if (valueType.name != "Never" && typeContainsScopeName(valueType) &&
                           !typeContainsScopeName(currentSignature ? currentSignature->returnType : makeUnknownType())) {
                    std::vector<std::string> valueScopes;
                    collectNamedScopeNames(valueType, valueScopes);
                    const std::string scopeName = valueScopes.empty() ? std::string{} : valueScopes.front();
                    if (!valueType.viewScope.empty()) {
                        reportError(
                            tailExprStmt,
                            "Scoped ref `" + valueType.describe() + "` cannot leave `scope " + valueType.viewScope +
                                "`. Return an owned value instead or keep the borrow inside that scope.");
                    } else if (!valueType.scopeName.empty()) {
                        reportError(
                            tailExprStmt,
                            "Implicit return cannot let value `" + valueType.describe() +
                                "` escape scope `" + valueType.scopeName + "`. Return an owned value, use Anchor[T], or keep the borrow inside the scope.");
                    } else {
                        reportError(
                            tailExprStmt,
                            "Implicit return cannot let value `" + valueType.describe() +
                                "` escape because it contains borrowed storage bound to scope `" + scopeName + "`.");
                    }
                } else if (valueType.name != "Never" && currentSignature &&
                           (currentSignature->returnType.isView() || isBorrowedStrMaybeType(currentSignature->returnType))) {
                    const bool returnTypeMatches = currentSignature->returnType.isView()
                        ? canBorrowAsView(valueType, currentSignature->returnType)
                        : canAssignType(valueType, currentSignature->returnType);
                    if (!returnTypeMatches) {
                        reportError(
                            tailExprStmt,
                            "Implicit return type mismatch in function '" + currentFunction->name + "': expected " +
                                currentSignature->returnType.describe() + ", got " + valueType.describe());
                    }
                    const auto sourceParamIndex = tailExprStmt->expr ? resolveViewSourceParam(tailExprStmt->expr.get()) : std::nullopt;
                    if (!sourceParamIndex.has_value()) {
                        reportError(
                            tailExprStmt,
                            "Returned ref value must come from one of the function's ref parameters. Hint: return a view derived from a ref parameter, or return an owned `String`/`String.from(...)` copy instead.");
                    } else if (sourceParamIndex.value() != SIZE_MAX) {
                        if (!currentViewReturnSourceParam.has_value() || currentViewReturnSourceParam.value() == SIZE_MAX) {
                            currentViewReturnSourceParam = sourceParamIndex;
                            currentViewReturnSeen = true;
                        } else if (currentViewReturnSourceParam != sourceParamIndex) {
                            reportError(tailExprStmt, "All returned ref values in a function must come from the same parameter.");
                        }
                    } else if (!currentViewReturnSeen) {
                        currentViewReturnSourceParam = sourceParamIndex;
                        currentViewReturnSeen = true;
                    }
                } else if (valueType.name != "Never" && currentSignature && currentFunction &&
                           !canAssignType(valueType, currentSignature->returnType)) {
                    reportError(
                        tailExprStmt,
                        "Implicit return type mismatch in function '" + currentFunction->name + "': expected " +
                            currentSignature->returnType.describe() + ", got " + valueType.describe());
                }

                if (valueType.name != "Never" && rawDepth == 0 && isRawAddressType(valueType)) {
                    reportError(tailExprStmt, "Raw address values may only appear inside raw blocks.");
                }
            }
        }
    }

    const bool unitReturn = currentSignature &&
        currentSignature->returnType.viewKind.empty() &&
        currentSignature->returnType.name == "Unit";

    if (currentSignature &&
        (currentSignature->returnType.isView() || isBorrowedStrMaybeType(currentSignature->returnType))) {
        auto signatureIt = analysisResult.functionSignatures.find(fn);
        if (signatureIt != analysisResult.functionSignatures.end()) {
            signatureIt->second.viewReturnSourceParam = currentViewReturnSourceParam;
            analysisResult.functionsByName[fn->name] = signatureIt->second;
        }
        if (!currentViewReturnSeen) {
            reportError(fn, "Borrowed-returning functions must return a view derived from one of their borrowed parameters.");
        }
    } else if (currentSignature && !unitReturn && !hasImplicitTailReturn && (!fn->body || !blockDefinitelyTerminates(fn->body.get()))) {
        reportError(
            fn,
            "Function '" + fn->name + "' must end with a value of type " +
                currentSignature->returnType.describe() + " or use explicit return.");
    }

    while (namedBorrowScopes.size() > previousNamedScopeCount) {
        namedBorrowScopes.pop_back();
    }
    activeNamedScopes.resize(previousActiveNamedScopeCount);
    currentFunction = previousFunction;
    currentSignature = previousSignature;
    implicitTailExpr = previousImplicitTailExpr;
    currentViewReturnSourceParam = previousViewReturnSourceParam;
    currentViewReturnSeen = previousViewReturnSeen;
    rawDepth = previousRawDepth;
    exitSemanticScope();
}

void SemanticAnalyzer::analyzeShapeDecl(ShapeDecl* shape) {
    ShapeInfo info;
    info.isViewShape = shape->isViewShape;
    info.scopeParamName = shape->scopeParamName;
    info.typeParams = shape->typeParams;

    std::unordered_set<std::string> localTypeParams(shape->typeParams.begin(), shape->typeParams.end());
    std::unordered_set<std::string> seenFields;

    for (const auto& field : shape->fields) {
        if (!seenFields.insert(field.name).second) {
            reportError(field.span, "Duplicate field in shape '" + shape->name + "': " + field.name);
            continue;
        }

        ResolvedType type = resolveTypeNode(field.type.get(), localTypeParams);
        const bool shapeFieldUsesDeclaredScope =
            shape->isViewShape && !shape->scopeParamName.empty() && type.isView() &&
            type.viewScope == shape->scopeParamName;
        if (!shapeFieldUsesDeclaredScope && !validateScopedViewType(type, field.type.get(), "shape fields")) {
            info.fields[field.name] = type;
            info.fieldOrder.push_back(field.name);
            continue;
        }
        if (shape->isViewShape) {
            if (shape->scopeParamName.empty()) {
                reportError(shape, "view shape '" + shape->name + "' requires a named scope parameter like [s].");
            } else if (typeContainsBorrowedStorage(type) && !typeUsesOnlyNamedScope(type, shape->scopeParamName)) {
                reportError(
                    field.span,
                    "view shape '" + shape->name + "' field '" + field.name +
                        "' must use the declared scope '" + shape->scopeParamName +
                        "' for all borrowed storage.");
            }
        } else if (typeContainsBorrowedStorage(type)) {
            reportError(
                field.span,
                "shape '" + shape->name + "' cannot store borrowed field '" + field.name + "' of type " +
                    type.describe() + ". Normal shapes may outlive borrowed data. Hint: use `String` for owned text, or keep `Str` local and pass it directly.");
        } else if (type.isView() && type.viewScope.empty()) {
            reportError(
                field.span,
                "Shape '" + shape->name + "' cannot store " + type.describe() +
                    " directly in field '" + field.name +
                    "'. Store an owned value, use Anchor[T] for stable storage, or keep the borrow inside a scope block.");
        }
        info.fields[field.name] = type;
        info.fieldOrder.push_back(field.name);
    }

    analysisResult.shapesByName[shape->name] = info;
    const std::string canonicalName = canonicalTypeName(shape->name);
    if (canonicalName != shape->name) {
        analysisResult.shapesByName[canonicalName] = info;
    }
}

void SemanticAnalyzer::analyzeChoiceDecl(ChoiceDecl* choice) {
    ChoiceInfo info;
    info.typeParams = choice->typeParams;

    std::unordered_set<std::string> localTypeParams(choice->typeParams.begin(), choice->typeParams.end());
    std::unordered_set<std::string> seenVariants;

    for (const auto& variant : choice->variants) {
        const bool reservedCapitalizedVariant =
            (choice->name == "Result" && (variant.tag == "Ok" || variant.tag == "Fail")) ||
            (choice->name == "Maybe" && (variant.tag == "Some" || variant.tag == "None"));
        if (!reservedCapitalizedVariant && !startsWithLowercase(variant.tag)) {
            std::string lowercaseSuggestion = variant.tag;
            lowercaseSuggestion.front() = static_cast<char>(
                std::tolower(static_cast<unsigned char>(lowercaseSuggestion.front())));
            reportError(
                variant.span,
                "Choice variant '" + variant.tag + "' must start with a lowercase letter; use '" +
                    lowercaseSuggestion + "'.");
        }

        if (!seenVariants.insert(variant.tag).second) {
            reportError(variant.span, "Duplicate variant in choice '" + choice->name + "': " + variant.tag);
            continue;
        }

        ChoiceVariantInfo variantInfo;
        for (const auto& payload : variant.payloads) {
            ResolvedType payloadType = resolveTypeNode(payload.type.get(), localTypeParams);
            if (containsOwnedTupleValue(payloadType)) {
                reportError(
                    payload.span,
                    "Choice payloads cannot contain tuples with owned fields until choice drop glue is available.");
            }
            if (!validateScopedViewType(payloadType, payload.type.get(), "choice payloads")) {
                variantInfo.payloadTypes.push_back(std::move(payloadType));
                continue;
            }
            if (payloadType.isView() && payloadType.viewScope.empty()) {
                reportError(
                    payload.span,
                    "Choice '" + choice->name + "' cannot store `" + payloadType.describe() +
                        "` directly in variant '" + variant.tag +
                        "'. Store an owned value, use Anchor[T] for stable storage, or keep the borrow inside a scope block.");
            }
            variantInfo.payloadTypes.push_back(std::move(payloadType));
        }

        info.variants[variant.tag] = std::move(variantInfo);
        info.variantOrder.push_back(variant.tag);
    }

    analysisResult.choicesByName[choice->name] = info;
    const std::string canonicalName = canonicalTypeName(choice->name);
    if (canonicalName != choice->name) {
        analysisResult.choicesByName[canonicalName] = info;
    }
}

void SemanticAnalyzer::analyzeBlock(BlockStmt* block) {
    for (size_t i = 0; i < block->statements.size(); ++i) {
        Stmt* stmt = block->statements[i].get();
        analyzeStmt(stmt);
        if (i + 1 < block->statements.size()) {
            warnNonTailValueExpression(stmt);
        }
    }
}

ResolvedType SemanticAnalyzer::analyzeBlockValue(BlockStmt* block, const ResolvedType* expectedType) {
    if (!block) {
        return makePlainType("Unit");
    }

    for (size_t i = 0; i < block->statements.size(); ++i) {
        Stmt* stmt = block->statements[i].get();
        if (i + 1 == block->statements.size()) {
            if (auto* tail = dynamic_cast<ExprStmt*>(stmt)) {
                return tail->expr
                    ? analyzeExpr(tail->expr.get(), expectedType, false, true)
                    : makePlainType("Unit");
            }
        }

        analyzeStmt(stmt);
        warnNonTailValueExpression(stmt);
        if (stmtDefinitelyTerminates(stmt)) {
            return makePlainType("Never");
        }
    }

    return makePlainType("Unit");
}

void SemanticAnalyzer::validateInferredIntegerLiteral(const Expr* expr, const std::string& targetName) {
    const auto* integerExpr = dynamic_cast<const IntExpr*>(expr);
    if (!integerExpr) {
        return;
    }

    const auto literal = parseIntegerLiteralText(integerExpr->value);
    if (literal.valid && literal.suffix.empty() &&
        !integerLiteralFitsTarget(literal.magnitude, targetName, target)) {
        reportError(
            integerExpr,
            "Integer literal '" + literal.magnitude + "' does not fit target type " + targetName + ".");
    }
}

ResolvedType SemanticAnalyzer::unifyBranchTypes(
    const AstNode* owner,
    const std::vector<ResolvedType>& branchTypes,
    const ResolvedType* expectedType,
    std::string_view construct) {
    bool hasValueBranch = false;
    ResolvedType commonType = makeUnknownType();

    const auto branchValue = [&](size_t index) -> const Expr* {
        const BlockStmt* block = nullptr;
        if (const auto* ifExpr = dynamic_cast<const IfExpr*>(owner)) {
            block = index == 0 ? ifExpr->thenBlock.get() : ifExpr->elseBlock.get();
        } else if (const auto* pickExpr = dynamic_cast<const PickExpr*>(owner)) {
            if (index < pickExpr->branches.size()) {
                block = pickExpr->branches[index].body.get();
            }
        }
        if (!block || block->statements.empty()) {
            return nullptr;
        }
        const auto* tail = dynamic_cast<const ExprStmt*>(block->statements.back().get());
        return tail ? tail->expr.get() : nullptr;
    };

    for (size_t branchIndex = 0; branchIndex < branchTypes.size(); ++branchIndex) {
        const auto& branchType = branchTypes[branchIndex];
        if (!expectedType && isIntegerLiteralType(branchType)) {
            validateInferredIntegerLiteral(branchValue(branchIndex), "Int32");
        }
        if (branchType.name == "Never") {
            continue;
        }
        if (branchType.isOpaqueExternal()) {
            return makeUnknownType();
        }

        if (expectedType) {
            const bool compatible = expectedType->isView()
                ? canBorrowAsView(branchType, *expectedType)
                : canAssignType(branchType, *expectedType);
            if (!compatible) {
                reportError(
                    owner,
                    "Branch type mismatch in " + std::string(construct) + ": expected " +
                        expectedType->describe() + ", got " + branchType.describe() + ".");
            }
            commonType = *expectedType;
            hasValueBranch = true;
            continue;
        }

        ResolvedType candidate = normalizeInferredLiteralType(branchType);
        if (!hasValueBranch || commonType.isUnknown()) {
            commonType = candidate;
            hasValueBranch = true;
            continue;
        }

        if (sameType(commonType, candidate)) {
            continue;
        }
        if (canAssignType(candidate, commonType)) {
            continue;
        }
        if (canAssignType(commonType, candidate)) {
            commonType = candidate;
            continue;
        }

        reportError(
            owner,
            "Incompatible branch types in " + std::string(construct) + ": " +
                commonType.describe() + " and " + candidate.describe() + ".");
        commonType = makeUnknownType();
    }

    return hasValueBranch ? commonType : makePlainType("Never");
}

void SemanticAnalyzer::applyBlockTailType(BlockStmt* block, const ResolvedType& type) {
    if (!block || block->statements.empty() || type.name == "Never" || type.isUnknown()) {
        return;
    }
    auto* tail = dynamic_cast<ExprStmt*>(block->statements.back().get());
    if (!tail || !tail->expr) {
        return;
    }
    const auto* actualType = lookupExprType(tail->expr.get());
    if (actualType && (isIntegerLiteralType(*actualType) || canAssignType(*actualType, type))) {
        analysisResult.exprTypes[tail->expr.get()] = type;
    }
}

bool SemanticAnalyzer::analyzeTuplePickPattern(
    const PickPattern* pattern,
    const ResolvedType& type,
    const std::string& path) {
    if (!pattern) {
        return true;
    }

    if (pattern->kind == PickPatternKind::Tuple) {
        if (type.name != "Tuple") {
            reportError(
                pattern->span,
                "Tuple pick pattern at " + path + " requires a tuple value, got " + type.describe() + ".");
            return false;
        }
        if (!isPlainTupleDestructuringField(type)) {
            reportError(
                pattern->span,
                "Tuple pick patterns currently support only plain, concrete, non-view fields; got " +
                    type.describe() + ".");
            return false;
        }
        if (pattern->elements.size() != type.params.size()) {
            reportError(
                pattern->span,
                "Tuple pick pattern at " + path + " expects " + std::to_string(pattern->elements.size()) +
                    " field(s), but the tuple has " + std::to_string(type.params.size()) + ".");
            return false;
        }

        bool valid = true;
        for (size_t i = 0; i < pattern->elements.size(); ++i) {
            const std::string childPath = path + "." + std::to_string(i);
            const bool childValid = analyzeTuplePickPattern(pattern->elements[i].get(), type.params[i], childPath);
            valid = childValid && valid;
        }
        return valid;
    }

    if (!isPlainTupleDestructuringField(type)) {
        reportError(
            pattern->span,
            "Tuple pick patterns currently support only plain, concrete, non-view fields; element " + path +
                " has type " + type.describe() + ".");
        return false;
    }

    return true;
}

void SemanticAnalyzer::bindTuplePickPattern(PickPattern* pattern, const ResolvedType& type) {
    if (!pattern) {
        return;
    }

    if (pattern->kind == PickPatternKind::Tuple) {
        for (size_t i = 0; i < pattern->elements.size(); ++i) {
            bindTuplePickPattern(pattern->elements[i].get(), type.params[i]);
        }
    } else if (pattern->kind == PickPatternKind::Binding) {
        defineVariable(
            pattern->name,
            type,
            false,
            "Duplicate pick binding: " + pattern->name,
            pattern->span,
            std::nullopt,
            nullptr,
            &pattern->backendBindingKey);
    }
}

void SemanticAnalyzer::analyzeStmt(Stmt* stmt) {
    if (auto* bind = dynamic_cast<BindingStmt*>(stmt)) {
        if (bind->tuplePattern) {
            if (bind->isMutable) {
                reportError(bind, "Tuple destructuring is currently supported only for immutable `val` bindings.");
            }
            if (!bind->value) {
                reportError(bind, "Tuple destructuring requires an initializer.");
                return;
            }

            const ResolvedType valueType = analyzeExpr(bind->value.get());
            if (valueType.isOpaqueExternal()) {
                reportError(bind->value.get(), opaqueExternalValueUseMessage(valueType, "tuple destructuring"));
                return;
            }
            if (valueType.name != "Tuple") {
                reportError(bind, "Tuple destructuring requires a tuple value, got " + valueType.describe() + ".");
                return;
            }
            if (valueType.isView() || !valueType.viewKind.empty()) {
                reportError(bind, "Tuple destructuring consumes the tuple value and cannot destructure a view tuple.");
                return;
            }
            if (bind->tuplePattern->kind != PickPatternKind::Tuple) {
                reportError(bind, "Tuple destructuring requires a tuple pattern.");
                return;
            }
            if (valueType.params.size() != bind->tuplePattern->elements.size()) {
                reportError(
                    bind,
                    "Tuple destructuring expects " + std::to_string(bind->tuplePattern->elements.size()) +
                        " binding(s), but the tuple has " + std::to_string(valueType.params.size()) + " field(s).");
                return;
            }

            const bool nestedPattern = hasNestedTuplePattern(bind->tuplePattern.get());
            if (nestedPattern && !isPlainTupleDestructuringField(valueType)) {
                reportError(
                    bind,
                    "Nested tuple destructuring currently supports only plain, concrete, non-view tuple values; got " +
                        valueType.describe() + ".");
                return;
            }

            std::function<void(PickPattern*, const ResolvedType&, const std::string&, bool)> bindPattern;
            bindPattern = [&](PickPattern* pattern, const ResolvedType& patternType, const std::string& path, bool root) {
                if (pattern->kind == PickPatternKind::Tuple) {
                    if (!root && patternType.name != "Tuple") {
                        reportError(
                            pattern->span,
                            "Nested tuple destructuring pattern at element " + path + " requires a tuple value, got " +
                                patternType.describe() + ".");
                        return;
                    }
                    if (pattern->elements.size() != patternType.params.size()) {
                        reportError(
                            pattern->span,
                            "Tuple destructuring pattern at element " + path + " expects " +
                                std::to_string(pattern->elements.size()) + " binding(s), but the tuple has " +
                                std::to_string(patternType.params.size()) + " field(s).");
                        return;
                    }
                    for (size_t i = 0; i < pattern->elements.size(); ++i) {
                        const std::string childPath = root
                            ? std::to_string(i)
                            : path + "." + std::to_string(i);
                        bindPattern(pattern->elements[i].get(), patternType.params[i], childPath, false);
                    }
                    return;
                }

                if (!isSupportedTupleDestructuringField(patternType)) {
                    reportError(
                        pattern->span,
                        "Tuple destructuring currently supports plain fields and Anchor[T] fields with plain T; element " +
                            path + " has type " + patternType.describe() + ".");
                    return;
                }
                if (pattern->kind == PickPatternKind::Binding) {
                    defineVariable(
                        pattern->name,
                        patternType,
                        false,
                        "Duplicate variable declaration: " + pattern->name,
                        pattern->span,
                        std::nullopt,
                        bind,
                        &pattern->backendBindingKey);
                }
            };
            bindPattern(bind->tuplePattern.get(), valueType, "", true);
            return;
        }

        const ResolvedType declaredType = bind->type ? resolveTypeNode(bind->type.get(), {}) : makeUnknownType();
        const ResolvedType valueType = bind->value
            ? analyzeExpr(bind->value.get(), bind->type ? &declaredType : nullptr)
            : makeUnknownType();
        const bool opaqueInitializer = bind->value && valueType.isOpaqueExternal();
        ResolvedType finalType = bind->type ? declaredType : normalizeInferredLiteralType(valueType);

        if (!bind->type && !bind->value && !bind->isMutable) {
            reportError(bind, "Binding requires a type or an initializer: " + bind->name);
            finalType = makeUnknownType();
        }

        if (!bind->isMutable && !bind->value) {
            reportError(bind, "Immutable binding requires an initializer: " + bind->name);
        }

        if (opaqueInitializer) {
            reportError(
                bind->value.get(),
                opaqueExternalValueUseMessage(valueType, "binding initializer for '" + bind->name + "'"));
            if (!bind->type) {
                finalType = makeUnknownType();
            }
        }

        if (bind->value && typeContainsBorrowedStorage(valueType) &&
            expressionReturnsBorrowFromOwnedTemporary(*this, bind->value.get())) {
            reportError(
                bind->value.get(),
                "Borrowed view cannot escape a temporary owner. Hint: keep the owner in a local variable, then derive the view from it.");
        }
        const bool bindingTypeMatches = finalType.isView()
            ? canPassArgumentType(bind->value.get(), valueType, finalType)
            : canAssignType(valueType, finalType);
        if (bind->type && bind->value && !opaqueInitializer && !bindingTypeMatches) {
            reportError(
                bind,
                "Initializer type mismatch for '" + bind->name + "': expected " +
                    finalType.describe() + ", got " + valueType.describe());
        }

        if (!bind->type && bind->value && isIntegerLiteralType(valueType)) {
            validateInferredIntegerLiteral(bind->value.get(), finalType.name);
            analysisResult.exprTypes[bind->value.get()] = finalType;
        }

        if (rawDepth == 0 && isRawAddressType(finalType)) {
            reportError(bind, "Raw address values may only appear inside raw blocks.");
        }
        validateScopedViewType(finalType, bind, "bindings");
        if (bind->value && finalType.isView() && !finalType.viewScope.empty()) {
            validateScopedBorrowSource(bind->value.get(), finalType, bind, "binding initializer");
        }
        if (bind->value && valueType.isView() && !valueType.viewScope.empty() &&
            (!finalType.isView() || finalType.viewScope.empty())) {
            reportError(
                bind,
                "Scoped ref `" + valueType.describe() + "` cannot escape into binding '" + bind->name +
                    "'. Keep it inside `scope " + valueType.viewScope + "` or store an owned value instead.");
        }

        std::vector<std::string> finalScopes;
        collectNamedScopeNames(finalType, finalScopes);
        for (const auto& scopeName : finalScopes) {
            if (!isNamedScopeActive(scopeName)) {
                reportError(bind, "Type " + finalType.describe() + " is bound to scope `" + scopeName + "`, but that scope is not active here.");
            }
        }
        std::vector<std::string> valueScopes;
        collectNamedScopeNames(valueType, valueScopes);
        for (const auto& scopeName : valueScopes) {
            if (!isNamedScopeActive(scopeName)) {
                reportError(bind, "Initializer value for '" + bind->name + "' carries scoped borrow `" + scopeName + "` outside its active scope.");
            }
        }

        const std::optional<size_t> viewSourceParamIndex =
            ((finalType.isView() || finalType.name == "Span" || typeContainsScopeName(finalType) ||
              isBorrowedStrMaybeType(finalType)) && bind->value)
            ? resolveViewSourceParam(bind->value.get())
            : std::nullopt;

        analysisResult.bindingTypes[bind] = finalType;
        defineVariable(
            bind->name,
            finalType,
            bind->isMutable,
            "Duplicate variable declaration: " + bind->name,
            bind->span,
            viewSourceParamIndex,
            bind);
        return;
    }

    if (auto* tryStmt = dynamic_cast<TryStmt*>(stmt)) {
        const ResolvedType declaredType = tryStmt->type ? resolveTypeNode(tryStmt->type.get(), {}) : makeUnknownType();
        const ResolvedType outcomeType = analyzeExpr(tryStmt->expr.get());
        ResolvedType okType = makeUnknownType();
        ResolvedType failType = makeUnknownType();

        if (tryStmt->isMutable) {
            reportError(
                tryStmt,
                "`try` is only allowed in a `val` initializer. Use `val t = try ...` and then `var x = t`.");
        }

        if (outcomeType.isOpaqueExternal()) {
            reportError(tryStmt->expr.get(), opaqueExternalValueUseMessage(outcomeType, "try operand"));
        } else if (!outcomeType.isUnknown()) {
            if (outcomeType.name != "Result" || outcomeType.params.size() != 2) {
                reportError(tryStmt->expr.get(), "`try` requires a Result value, got " + outcomeType.describe());
            } else {
                const auto* outcomeInfo = lookupChoice(outcomeType.name);
                if (!outcomeInfo || !choice_utils::isOutcomeLikeChoice(*outcomeInfo)) {
                    reportError(tryStmt->expr.get(), "`try` requires Result to define Ok(T) and Fail(E) single-payload variants.");
                }
                okType = outcomeType.params[0];
                failType = outcomeType.params[1];
            }
        }

        ResolvedType finalType = tryStmt->type ? declaredType : normalizeInferredLiteralType(okType);
        if (tryStmt->type && !okType.isUnknown()) {
            const bool matches = finalType.isView()
                ? canBorrowAsView(okType, finalType)
                : canAssignType(okType, finalType);
            if (!matches) {
                reportError(
                    tryStmt,
                    "`try` binding type mismatch for '" + tryStmt->name + "': expected " +
                        finalType.describe() + ", got " + okType.describe());
            }
        }

        if (tryStmt->autoPropagate) {
            if (!currentSignature || currentSignature->returnType.name != "Result" || currentSignature->returnType.params.size() != 2) {
                reportError(tryStmt, "`try` shorthand requires the current function to return Result[T, E].");
            } else if (!failType.isUnknown()) {
                const ResolvedType& currentFailType = currentSignature->returnType.params[1];
                if (!sameType(failType, currentFailType)) {
                    reportError(
                        tryStmt,
                        "`try` shorthand error type mismatch: expected " +
                            currentFailType.describe() + ", got " + failType.describe());
                }
            }
        } else {
            if (!tryStmt->failBlock || tryStmt->failName.empty()) {
                reportError(tryStmt, "`try ... else` requires a failure binding and block.");
            } else {
                if (!blockDefinitelyTerminates(tryStmt->failBlock.get())) {
                    reportError(tryStmt, "`try ... else` failure block must terminate the current control path.");
                }

                enterSemanticScope();
                defineVariable(
                    tryStmt->failName,
                    failType,
                    false,
                    "Duplicate try failure binding: " + tryStmt->failName,
                    tryStmt->span,
                    std::nullopt,
                    nullptr,
                    &tryStmt->failBindingKey);
                analyzeBlock(tryStmt->failBlock.get());
                exitSemanticScope();
            }
        }

        if (rawDepth == 0 && isRawAddressType(finalType)) {
            reportError(tryStmt, "Raw address values may only appear inside raw blocks.");
        }
        if (!finalType.scopeName.empty() && !isNamedScopeActive(finalType.scopeName)) {
            reportError(tryStmt, "Type " + finalType.describe() + " is bound to scope `" + finalType.scopeName + "`, but that scope is not active here.");
        }
        validateScopedViewType(finalType, tryStmt, "`try` bindings");
        if (finalType.isView() && !finalType.viewScope.empty()) {
            validateScopedBorrowSource(tryStmt->expr.get(), finalType, tryStmt, "`try` binding");
        }

        analysisResult.tryBindingTypes[tryStmt] = finalType;
        defineVariable(
            tryStmt->name,
            finalType,
            false,
            "Duplicate variable declaration: " + tryStmt->name,
            tryStmt->span,
            std::nullopt,
            nullptr,
            &tryStmt->backendBindingKey);
        return;
    }

    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        const ResolvedType targetType = assign->target ? analyzeExpr(assign->target.get()) : makeUnknownType();
        const ResolvedType valueType = assign->value ? analyzeExpr(assign->value.get(), &targetType) : makeUnknownType();

        if (assign->op != "=") {
            const bool integerTarget = isIntegerLikeTypeName(targetType.name) || isIntegerLiteralType(targetType);
            const bool numericTarget = isNumericTypeName(targetType.name) || isNumericLiteralType(targetType);
            if ((assign->op == "%=" && !integerTarget) || (assign->op != "%=" && !numericTarget)) {
                reportError(
                    assign,
                    "Compound assignment '" + assign->op + "' requires " +
                        (assign->op == "%=" ? "an integer target." : "a numeric target."));
            }
        }

        if (rawDepth == 0 && (isRawAddressType(valueType) || isRawAddressType(targetType))) {
            reportError(assign, "Raw address values may only appear inside raw blocks.");
        }

        const bool opaqueAssignedValue = assign->value && valueType.isOpaqueExternal();

        if (assign->value && typeContainsBorrowedStorage(valueType) &&
            expressionReturnsBorrowFromOwnedTemporary(*this, assign->value.get())) {
            reportError(
                assign->value.get(),
                "Borrowed view cannot escape a temporary owner. Hint: keep the owner in a local variable, then derive the view from it.");
        }

        if (auto* ident = dynamic_cast<IdentExpr*>(assign->target.get())) {
            const auto sym = lookupSymbol(ident->name);
            if (!sym) {
                reportError(ident, "Undefined variable: " + ident->name);
            } else {
                if (sym->kind != SymbolKind::Variable) {
                    reportError(assign, "Assignment target is not mutable storage: " + ident->name);
                } else if (!sym->isMutable) {
                    reportError(assign, "Cannot assign to immutable binding: " + ident->name);
                }

                if (opaqueAssignedValue) {
                    reportError(
                        assign->value.get(),
                        opaqueExternalValueUseMessage(valueType, "assignment to '" + ident->name + "'"));
                } else {
                    std::vector<std::string> valueScopes;
                    collectNamedScopeNames(valueType, valueScopes);
                    for (const auto& scopeName : valueScopes) {
                        if (!symbolCanStoreNamedScope(*sym, scopeName)) {
                            reportError(
                                assign,
                                "Cannot store value bound to scope `" + scopeName +
                                    "` in binding '" + ident->name +
                                    "' declared outside that scope. Move the binding inside the scope or return an owned value.");
                        }
                    }

                    if (sym->type.isUnknown()) {
                        const ResolvedType inferredType = normalizeInferredLiteralType(valueType);
                        if (!inferredType.isUnknown()) {
                            if (isIntegerLiteralType(valueType)) {
                                validateInferredIntegerLiteral(assign->value.get(), inferredType.name);
                            }
                            sym->type = inferredType;
                            if (sym->bindingDecl) {
                                analysisResult.bindingTypes[sym->bindingDecl] = inferredType;
                            }
                            if (isNumericLiteralType(valueType)) {
                                analysisResult.exprTypes[assign->value.get()] = inferredType;
                            }
                            if (inferredType.isView() || typeContainsScopeName(inferredType)) {
                                sym->viewSourceParamIndex = resolveViewSourceParam(assign->value.get());
                            } else {
                                sym->viewSourceParamIndex.reset();
                            }
                        }
                    }

                    const bool assignmentTypeMatches = sym->type.isView()
                        ? canPassArgumentType(assign->value.get(), valueType, sym->type)
                        : canAssignType(valueType, sym->type);
                    if (!assignmentTypeMatches) {
                        reportError(
                            assign,
                            "Assigned value type mismatch for '" + ident->name + "': expected " +
                                sym->type.describe() + ", got " + valueType.describe());
                    }
                    if (sym->type.isView() && !sym->type.viewScope.empty()) {
                        validateScopedBorrowSource(assign->value.get(), sym->type, assign, "assignment");
                    }
                    if (valueType.isView() && !valueType.viewScope.empty() &&
                        (!sym->type.isView() || sym->type.viewScope.empty())) {
                        reportError(
                            assign,
                            "Scoped ref `" + valueType.describe() + "` cannot escape into `" + ident->name +
                                "`. Keep it inside `scope " + valueType.viewScope + "` or store an owned value instead.");
                    }

                    if (sym->type.isView() || typeContainsScopeName(sym->type)) {
                        sym->viewSourceParamIndex = resolveViewSourceParam(assign->value.get());
                    }
                }
            }
        } else if (auto* member = dynamic_cast<MemberExpr*>(assign->target.get())) {
            bool canMutate = false;
            if (const auto* objectType = lookupExprType(member->object.get())) {
                if (objectType->viewKind == "edit") {
                    canMutate = true;
                }
            }

            if (auto* objectIdent = dynamic_cast<IdentExpr*>(member->object.get())) {
                const auto sym = lookupSymbol(objectIdent->name);
                if (sym && sym->kind == SymbolKind::Variable && sym->isMutable) {
                    canMutate = true;
                }
            }

            if (!canMutate) {
                reportError(assign, "Member assignment requires mutable storage or a ref mut view.");
            }

            if (const auto* objectType = lookupExprType(member->object.get());
                objectType && objectType->name == "Tuple" && targetType.isOwned()) {
                reportError(
                    assign,
                    "Replacing an owned tuple field directly is unsupported; destructure the tuple and rebuild it instead.");
            }

            if (opaqueAssignedValue) {
                reportError(assign->value.get(), opaqueExternalValueUseMessage(valueType, "member assignment"));
            } else if (!canAssignType(valueType, targetType)) {
                reportError(
                    assign,
                    "Assigned value type mismatch for member access: expected " +
                        targetType.describe() + ", got " + valueType.describe());
            }
        } else if (auto* index = dynamic_cast<IndexExpr*>(assign->target.get())) {
            const auto* indexObjectType = lookupExprType(index->object.get());
            if (indexObjectType && indexObjectType->name == "Str") {
                reportError(assign, "Indexed assignment is not supported for Str; string indexing is read-only.");
            } else if (indexObjectType && indexObjectType->name == "Array") {
                if (assign->op != "=") {
                    reportError(assign, "Compound assignment to fixed Array elements is out of scope; use plain indexed assignment.");
                } else if (!isSupportedFixedArrayType(*indexObjectType)) {
                    reportError(assign, "Indexed assignment is supported only for fixed Array[Int32, N] and Array[UInt8, N].");
                } else {
                    const auto* arrayIdent = dynamic_cast<const IdentExpr*>(index->object.get());
                    const auto arraySymbol = arrayIdent ? lookupSymbol(arrayIdent->name) : nullptr;
                    if (!arraySymbol || arraySymbol->kind != SymbolKind::Variable || !arraySymbol->isMutable ||
                        arraySymbol->isStatic || arraySymbol->bindingDecl == nullptr) {
                        reportError(assign, "Fixed Array element assignment requires a local mutable `var` binding.");
                    }
                }
            } else if (!canBorrowExprAsEdit(index)) {
                reportError(assign, "Index assignment requires mutable storage or a ref mut view.");
            }
            if (opaqueAssignedValue) {
                reportError(assign->value.get(), opaqueExternalValueUseMessage(valueType, "index assignment"));
            } else if (!canAssignType(valueType, targetType)) {
                reportError(
                    assign,
                    "Assigned value type mismatch for index access: expected " +
                        targetType.describe() + ", got " + valueType.describe());
            }
        } else {
            reportError(assign, "Unsupported assignment target.");
        }
        return;
    }

    if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        const ResolvedType valueType = returnStmt->value
            ? analyzeExpr(returnStmt->value.get(), currentSignature ? &currentSignature->returnType : nullptr)
            : makePlainType("Unit");
        if (returnStmt->value && typeContainsBorrowedStorage(valueType) &&
            expressionReturnsBorrowFromOwnedTemporary(*this, returnStmt->value.get())) {
            reportError(
                returnStmt->value.get(),
                "Borrowed view cannot escape a temporary owner. Hint: keep the owner in a local variable, then derive the view from it.");
        } else if (valueType.isOpaqueExternal()) {
            reportError(returnStmt->value.get(), opaqueExternalValueUseMessage(valueType, "return value"));
        } else if (typeContainsScopeName(valueType) &&
                   !(currentSignature && typeContainsScopeName(currentSignature->returnType))) {
            std::vector<std::string> valueScopes;
            collectNamedScopeNames(valueType, valueScopes);
            const std::string scopeName = valueScopes.empty() ? std::string{} : valueScopes.front();
            if (!valueType.viewScope.empty()) {
                reportError(
                    returnStmt,
                    "Scoped ref `" + valueType.describe() + "` cannot leave `scope " + valueType.viewScope +
                        "`. Return an owned value instead or keep the borrow inside that scope.");
            } else if (!valueType.scopeName.empty()) {
                reportError(
                    returnStmt,
                    "Cannot return value `" + valueType.describe() + "` because it is bound to scope `" +
                        valueType.scopeName + "`. Return owned data or keep the borrowed aggregate inside the scope.");
            } else {
                reportError(
                    returnStmt,
                    "Cannot return value `" + valueType.describe() +
                        "` because it contains borrowed storage bound to scope `" + scopeName + "`.");
            }
        } else if (currentSignature &&
                   (currentSignature->returnType.isView() || isBorrowedStrMaybeType(currentSignature->returnType))) {
            const bool returnTypeMatches = currentSignature->returnType.isView()
                ? canBorrowAsView(valueType, currentSignature->returnType)
                : canAssignType(valueType, currentSignature->returnType);
            if (!returnTypeMatches) {
                reportError(
                    returnStmt,
                    "Return type mismatch in function '" + currentFunction->name + "': expected " +
                        currentSignature->returnType.describe() + ", got " + valueType.describe());
            }
            const auto sourceParamIndex = returnStmt->value ? resolveViewSourceParam(returnStmt->value.get()) : std::nullopt;
            if (!sourceParamIndex.has_value()) {
                reportError(returnStmt, "Returned ref value must come from one of the function's ref parameters. Hint: return a view derived from a ref parameter, or return an owned `String`/`String.from(...)` copy instead.");
            } else if (sourceParamIndex.value() != SIZE_MAX) {
                if (!currentViewReturnSourceParam.has_value() || currentViewReturnSourceParam.value() == SIZE_MAX) {
                    currentViewReturnSourceParam = sourceParamIndex;
                    currentViewReturnSeen = true;
                } else if (currentViewReturnSourceParam != sourceParamIndex) {
                    reportError(returnStmt, "All returned ref values in a function must come from the same parameter.");
                }
            } else if (!currentViewReturnSeen) {
                currentViewReturnSourceParam = sourceParamIndex;
                currentViewReturnSeen = true;
            }
        } else if (currentSignature && currentFunction && !canAssignType(valueType, currentSignature->returnType)) {
            reportError(
                returnStmt,
                "Return type mismatch in function '" + currentFunction->name + "': expected " +
                    currentSignature->returnType.describe() + ", got " + valueType.describe());
        }

        if (rawDepth == 0 && isRawAddressType(valueType)) {
            reportError(returnStmt, "Raw address values may only appear inside raw blocks.");
        }
        return;
    }

    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (exprStmt->expr) {
            const bool isTail = isImplicitTailExpr(exprStmt);
            const bool valueTail = isTail && currentSignature && currentSignature->returnType.name != "Unit";
            const bool isControlValue = dynamic_cast<IfExpr*>(exprStmt->expr.get()) ||
                                        dynamic_cast<PickExpr*>(exprStmt->expr.get());
            const bool isTupleReturn = currentSignature && currentSignature->returnType.name == "Tuple" &&
                                       currentSignature->returnType.viewKind.empty();
            const bool isTupleLiteral = dynamic_cast<TupleExpr*>(exprStmt->expr.get()) != nullptr;
            const bool needsTupleReturnContext = valueTail && isTupleReturn && isTupleLiteral;
            const ResolvedType* expectedType = valueTail && (isControlValue || needsTupleReturnContext)
                ? &currentSignature->returnType
                : nullptr;
            const ResolvedType exprType = analyzeExpr(exprStmt->expr.get(), expectedType, false, valueTail);
            if (rawDepth == 0 && isRawAddressType(exprType)) {
                reportError(exprStmt, "Raw address values may only appear inside raw blocks.");
            }
            if (!isImplicitTailExpr(exprStmt) && typeIsMustUse(exprType)) {
                reportError(
                    exprStmt,
                    "Unused " + exprType.describe() + " value. Handle it with `try`, `pick`, or bind it to `_` explicitly if you intend to ignore it.");
            }
        }
        return;
    }

    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        const ResolvedType conditionType = analyzeExpr(ifStmt->condition.get());
        if (conditionType.isOpaqueExternal()) {
            reportError(ifStmt->condition.get(), opaqueExternalValueUseMessage(conditionType, "'if' condition"));
        } else if (!isConditionLike(conditionType)) {
            reportError(ifStmt->condition.get(), "'if' condition must be Bool, got " + conditionType.describe());
        }

        enterSemanticScope();
        analyzeBlock(ifStmt->thenBlock.get());
        exitSemanticScope();

        if (ifStmt->elseBlock) {
            enterSemanticScope();
            analyzeBlock(ifStmt->elseBlock.get());
            exitSemanticScope();
        }
        return;
    }

    if (auto* loop = dynamic_cast<LoopStmt*>(stmt)) {
        if (loop->condition) {
            const ResolvedType conditionType = analyzeExpr(loop->condition.get());
            if (conditionType.isOpaqueExternal()) {
                reportError(loop->condition.get(), opaqueExternalValueUseMessage(conditionType, "'loop' condition"));
            } else if (!isConditionLike(conditionType)) {
                reportError(loop->condition.get(), "'loop' condition must be Bool, got " + conditionType.describe());
            }
        }

        enterSemanticScope();
        ++loopDepth;
        analyzeBlock(loop->body.get());
        --loopDepth;
        exitSemanticScope();
        return;
    }

    if (auto* scan = dynamic_cast<ScanStmt*>(stmt)) {
        const ResolvedType iterableType = analyzeExpr(scan->iterable.get());
        if (iterableType.isOpaqueExternal()) {
            reportError(scan->iterable.get(), opaqueExternalValueUseMessage(iterableType, "scan iterable"));
        }
        const bool supportedFixedArray = isSupportedFixedArrayType(iterableType);
        ResolvedType itemType = makeUnknownType();
        if (iterableType.name == "Str") {
            itemType = makePlainType("UInt8");
        } else if (!iterableType.params.empty()) {
            itemType = iterableType.params.front();
        }
        analysisResult.scanItemTypes[scan] = itemType;

        enterSemanticScope();
        ++loopDepth;
        scan->itemBindingKey = "$scan.binding." + std::to_string(nextScanBindingIdentity++);
        defineVariable(scan->itemName, itemType, false, "Duplicate scan variable: " + scan->itemName, scan->span);
        if (auto* currentScope = scopes.currentScope()) {
            if (const auto itemSymbol = currentScope->lookupLocal(scan->itemName)) {
                itemSymbol->backendBindingKey = scan->itemBindingKey;
            }
        }
        if (!scan->indexName.empty()) {
            const ResolvedType indexType = makePlainType("USize");
            scan->indexBindingKey = "$scan.binding." + std::to_string(nextScanBindingIdentity++);
            analysisResult.scanIndexTypes[scan] = indexType;
            if (!isSupportedRangeType(iterableType) && !supportedFixedArray) {
                reportError(scan, "An indexed scan is supported only over supported Range values or fixed Array[Int32, N] and Array[UInt8, N].");
            }
            defineVariable(
                scan->indexName,
                indexType,
                false,
                "Duplicate scan variable: " + scan->indexName,
                scan->span);
            if (auto* currentScope = scopes.currentScope()) {
                if (const auto indexSymbol = currentScope->lookupLocal(scan->indexName)) {
                    indexSymbol->backendBindingKey = scan->indexBindingKey;
                }
            }
        }
        analyzeBlock(scan->body.get());
        --loopDepth;
        exitSemanticScope();
        return;
    }

    if (auto* scopeStmt = dynamic_cast<ScopeStmt*>(stmt)) {
        enterSemanticScope();
        const bool namedScopeActive = enterNamedBorrowScope(scopeStmt->name, scopeStmt->span);
        if (namedScopeActive) {
            activeNamedScopes.push_back(scopeStmt->name);
        }
        analyzeBlock(scopeStmt->body.get());
        if (namedScopeActive) {
            activeNamedScopes.pop_back();
            exitNamedBorrowScope();
        }
        exitSemanticScope();
        return;
    }

    if (auto* pick = dynamic_cast<PickStmt*>(stmt)) {
        const ResolvedType valueType = analyzeExpr(pick->value.get());
        if (valueType.isOpaqueExternal()) {
            reportError(pick->value.get(), opaqueExternalValueUseMessage(valueType, "pick value"));
        }
        if (valueType.name == "Tuple" && valueType.isOwned() &&
            !dynamic_cast<IdentExpr*>(pick->value.get())) {
            reportError(pick->value.get(), "Pick over a temporary owned tuple is unsupported; bind it to a local first.");
        }
        const bool tupleValue = valueType.name == "Tuple";
        if (tupleValue) {
            bool tuplePatternSeen = false;
            bool wildcardSeen = false;
            for (size_t branchIndex = 0; branchIndex < pick->branches.size(); ++branchIndex) {
                const auto& branch = pick->branches[branchIndex];
                const bool isWildcard = branch.tag == "_";

                if (tuplePatternSeen) {
                    reportError(branch.span, "Unreachable pick branch after an irrefutable tuple pattern.");
                }
                if (wildcardSeen) {
                    if (isWildcard) {
                        reportError(branch.span, "Duplicate pick branch: _");
                    } else {
                        reportError(branch.span, "Unreachable pick branch after the '_' catch-all branch.");
                    }
                }

                if (isWildcard) {
                    if (branchIndex + 1 != pick->branches.size()) {
                        reportError(branch.span, "The '_' catch-all branch must be the last branch in a pick.");
                    }
                    if (!branch.bindings.empty()) {
                        reportError(branch.span, "The '_' catch-all branch cannot bind payload values.");
                    }
                    wildcardSeen = true;
                }

                enterSemanticScope();
                if (branch.tuplePattern && !wildcardSeen) {
                    if (analyzeTuplePickPattern(branch.tuplePattern.get(), valueType, "value")) {
                        bindTuplePickPattern(branch.tuplePattern.get(), valueType);
                    } else {
                        for (const auto& binding : branch.bindings) {
                            defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                        }
                    }
                    tuplePatternSeen = true;
                } else if (branch.tuplePattern) {
                    for (const auto& binding : branch.bindings) {
                        defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                    }
                } else if (!isWildcard && !branch.tuplePattern && !wildcardSeen) {
                    reportError(branch.span, "A pick over a tuple must use a tuple pattern or the '_' catch-all.");
                }
                analyzeBlock(branch.body.get());
                exitSemanticScope();
            }

            if (!wildcardSeen && !tuplePatternSeen) {
                reportError(pick, "Non-exhaustive pick for tuple type '" + valueType.describe() + "'.");
            }
            return;
        }
        const ChoiceInfo* choiceInfo = (valueType.isUnknown() || valueType.isOpaqueExternal()) ? nullptr : lookupChoice(valueType.name);
        std::unordered_set<std::string> seenTags;
        bool wildcardSeen = false;

        std::unordered_map<std::string, ResolvedType> choiceBindings;
        if (choiceInfo) {
            choiceBindings = buildTypeBindingsChecked(
                choiceInfo->typeParams,
                valueType.params,
                "choice '" + valueType.name + "'");
        }

        for (size_t branchIndex = 0; branchIndex < pick->branches.size(); ++branchIndex) {
            auto& branch = pick->branches[branchIndex];
            const bool isWildcard = branch.tag == "_";
            if (isWildcard) {
                if (wildcardSeen) {
                    reportError(branch.span, "Duplicate pick branch: _");
                }
                if (branchIndex + 1 != pick->branches.size()) {
                    reportError(branch.span, "The '_' catch-all branch must be the last branch in a pick.");
                }
                if (!branch.bindings.empty()) {
                    reportError(branch.span, "The '_' catch-all branch cannot bind payload values.");
                }
                wildcardSeen = true;
            } else if (wildcardSeen) {
                reportError(branch.span, "Unreachable pick branch after the '_' catch-all branch.");
            } else if (!seenTags.insert(branch.tag).second) {
                reportError(branch.span, "Duplicate pick branch: " + branch.tag);
            }

            enterSemanticScope();

            if (branch.tuplePattern) {
                analyzeTuplePickPattern(branch.tuplePattern.get(), valueType, "value");
                for (const auto& binding : branch.bindings) {
                    defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                }
            } else if (choiceInfo && isWildcard) {
                for (const auto& binding : branch.bindings) {
                    defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                }
            } else if (choiceInfo && !wildcardSeen) {
                const auto variantIt = choiceInfo->variants.find(branch.tag);
                if (variantIt == choiceInfo->variants.end()) {
                    reportError(branch.span, "Unknown choice variant '" + branch.tag + "' for type '" + valueType.name + "'.");
                } else {
                    const auto& payloadTypes = variantIt->second.payloadTypes;
                    branch.bindingKeys.resize(branch.bindings.size());
                    if (payloadTypes.size() != branch.bindings.size()) {
                        reportError(
                            branch.span,
                            "Branch '" + branch.tag + "' expects " + std::to_string(payloadTypes.size()) +
                                " binding(s), got " + std::to_string(branch.bindings.size()));
                    }
                    for (size_t i = 0; i < branch.bindings.size(); ++i) {
                        const ResolvedType payloadType = i < payloadTypes.size()
                            ? substituteType(payloadTypes[i], choiceBindings)
                            : makeUnknownType();
                        defineVariable(
                            branch.bindings[i],
                            payloadType,
                            false,
                            "Duplicate pick binding: " + branch.bindings[i],
                            branch.span,
                            std::nullopt,
                            nullptr,
                            &branch.bindingKeys[i]);
                    }
                }
            } else {
                for (const auto& binding : branch.bindings) {
                    defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                }
            }

            analyzeBlock(branch.body.get());
            exitSemanticScope();
        }

        if (choiceInfo && !wildcardSeen && seenTags.size() != choiceInfo->variantOrder.size()) {
            reportError(pick, "Non-exhaustive pick for choice type '" + valueType.name + "'.");
        }
        return;
    }

    if (auto* raw = dynamic_cast<RawStmt*>(stmt)) {
        enterSemanticScope();
        ++rawDepth;
        analyzeBlock(raw->body.get());
        --rawDepth;
        exitSemanticScope();
        return;
    }

    if (dynamic_cast<StopStmt*>(stmt)) {
        if (loopDepth == 0) {
            reportError(stmt, "'stop' is only valid inside loops.");
        }
        return;
    }

    if (dynamic_cast<SkipStmt*>(stmt)) {
        if (loopDepth == 0) {
            reportError(stmt, "'skip' is only valid inside loops.");
        }
        return;
    }
}

ResolvedType SemanticAnalyzer::analyzeExpr(
    Expr* expr,
    const ResolvedType* expectedType,
    bool allowNegativeIntegerMagnitude,
    bool valueRequired) {
    ResolvedType type = makeUnknownType();

    if (dynamic_cast<BoolExpr*>(expr)) {
        type = makePlainType("Bool");
    } else if (dynamic_cast<CharExpr*>(expr)) {
        type = makePlainType("Char");
    } else if (auto* intExpr = dynamic_cast<IntExpr*>(expr)) {
        const auto literal = parseIntegerLiteralText(intExpr->value);
        if (!literal.valid) {
            reportError(intExpr, "Malformed integer literal '" + intExpr->value + "'.");
        } else {
            const auto fitsTarget = [&](const std::string& targetName) {
                return allowNegativeIntegerMagnitude
                    ? integerLiteralFitsNegatedTarget(literal.magnitude, targetName, target)
                    : integerLiteralFitsTarget(literal.magnitude, targetName, target);
            };
            if (!literal.suffix.empty()) {
                const auto suffixType = resolveNumericLiteralSuffixType(literal.suffix);
                if (!suffixType.has_value()) {
                    reportError(intExpr, "Unknown numeric literal suffix: " + literal.suffix);
                } else {
                    type = *suffixType;
                    if (!fitsTarget(type.name)) {
                        reportError(intExpr, "Integer literal '" + literal.magnitude + "' does not fit target type " + type.name + ".");
                    }
                }
            } else if (shouldUseExpectedIntegerType(expectedType) || shouldUseExpectedFloatType(expectedType)) {
                type = *expectedType;
                if (!fitsTarget(type.name)) {
                    const std::string detail = shouldUseExpectedFloatType(expectedType)
                        ? "does not fit exactly in target type "
                        : "does not fit target type ";
                    reportError(intExpr, "Integer literal '" + literal.magnitude + "' " + detail + type.name + ".");
                }
            } else {
                type = makeIntegerLiteralType();
            }
        }
    } else if (auto* floatExpr = dynamic_cast<FloatExpr*>(expr)) {
        const auto literal = splitNumericLiteralText(floatExpr->value);
        if (!isValidFloatLiteralText(floatExpr->value)) {
            reportError(floatExpr, "Malformed float literal '" + floatExpr->value + "'.");
        } else if (!literal.suffix.empty()) {
            const auto suffixType = resolveNumericLiteralSuffixType(literal.suffix);
            if (!suffixType.has_value()) {
                reportError(floatExpr, "Unknown numeric literal suffix: " + literal.suffix);
            } else if (!isFloatTypeName(suffixType->name)) {
                reportError(floatExpr, "Float literal suffix must be Float32 or Float64, got " + literal.suffix + ".");
                type = *suffixType;
            } else {
                type = *suffixType;
                if (!floatLiteralFitsTarget(literal.magnitude, type.name)) {
                    reportError(floatExpr, "Float literal '" + literal.magnitude + "' does not fit target type " + type.name + ".");
                }
            }
        } else if (shouldUseExpectedFloatType(expectedType)) {
            type = *expectedType;
            if (!floatLiteralFitsTarget(literal.magnitude, type.name)) {
                reportError(floatExpr, "Float literal '" + literal.magnitude + "' does not fit target type " + type.name + ".");
            }
        } else {
            type = makeFloatLiteralType();
        }
    } else if (dynamic_cast<StringExpr*>(expr)) {
        type = makeStrViewType();
    } else if (auto* tupleExpr = dynamic_cast<TupleExpr*>(expr)) {
        if (tupleExpr->elements.empty()) {
            type = makePlainType("Unit");
        } else {
            ResolvedType tupleType;
            tupleType.name = "Tuple";
            tupleType.category = TypeCategory::Plain;
            const bool expectedTuple = expectedType && expectedType->name == "Tuple" && expectedType->viewKind.empty();
            const bool expectedArityMatches = expectedTuple && expectedType->params.size() == tupleExpr->elements.size();
            if (expectedTuple && !expectedArityMatches) {
                reportError(
                    tupleExpr,
                    "Tuple literal has " + std::to_string(tupleExpr->elements.size()) +
                        " element(s), but the expected tuple type has " +
                        std::to_string(expectedType->params.size()) + ".");
            }

            bool hasUnsupportedElement = false;
            bool hasOwnedElement = false;
            for (size_t i = 0; i < tupleExpr->elements.size(); ++i) {
                const ResolvedType* expectedElement = expectedArityMatches ? &expectedType->params[i] : nullptr;
                ResolvedType elementType = analyzeExpr(tupleExpr->elements[i].get(), expectedElement);
                if (isNumericLiteralType(elementType)) {
                    if (isIntegerLiteralType(elementType)) {
                        validateInferredIntegerLiteral(tupleExpr->elements[i].get(), "Int32");
                    }
                    elementType = normalizeInferredLiteralType(elementType);
                    analysisResult.exprTypes[tupleExpr->elements[i].get()] = elementType;
                }
                if (!isSupportedTupleDestructuringField(elementType)) {
                    reportError(
                        tupleExpr->elements[i].get(),
                        "Tuple fields must be concrete plain values or Anchor[T] with concrete plain T; views, Span, generic fields, and other owned types are not supported.");
                    hasUnsupportedElement = true;
                }
                hasOwnedElement = elementType.isOwned() || hasOwnedElement;
                tupleType.params.push_back(std::move(elementType));
            }
            if (hasUnsupportedElement || hasOwnedElement) {
                tupleType.category = TypeCategory::Owned;
            }
            type = std::move(tupleType);
        }
    } else if (auto* arrayExpr = dynamic_cast<ArrayExpr*>(expr)) {
            if (arrayExpr->elements.empty()) {
                reportError(arrayExpr, "Array literal must contain at least one element; zero-length arrays are not supported.");
            }

            const bool expectedArray = expectedType && expectedType->name == "Array" &&
                expectedType->params.size() == 2 && expectedType->isPlain();
            std::vector<ResolvedType> elementTypes;
            elementTypes.reserve(arrayExpr->elements.size());
            for (const auto& element : arrayExpr->elements) {
                elementTypes.push_back(analyzeExpr(element.get(), expectedArray ? &expectedType->params.front() : nullptr));
            }

            ResolvedType elementType = expectedArray ? expectedType->params.front() : makeUnknownType();
            if (!expectedArray) {
                for (const auto& candidate : elementTypes) {
                    if (isIntegerLiteralType(candidate) || candidate.isUnknown()) {
                        continue;
                    }
                    if (elementType.isUnknown()) {
                        elementType = candidate;
                    } else if (candidate.describe() != elementType.describe()) {
                        reportError(
                            arrayExpr,
                            "Fixed Array literal elements must have one matching element type; expected " +
                                elementType.describe() + ", got " + candidate.describe() + ".");
                    }
                }
                if (elementType.isUnknown() || (elementType.name != "UInt8" && elementType.name != "Int32")) {
                    elementType = makePlainType("Int32");
                }
            }

            for (size_t i = 0; i < arrayExpr->elements.size(); ++i) {
                auto& element = arrayExpr->elements[i];
                ResolvedType& actualType = elementTypes[i];
                if (isIntegerLiteralType(actualType)) {
                    validateInferredIntegerLiteral(element.get(), elementType.name);
                    actualType = elementType;
                    analysisResult.exprTypes[element.get()] = actualType;
                }
                if (!actualType.isUnknown() && !canAssignType(actualType, elementType)) {
                    reportError(
                        element.get(),
                        "Fixed Array literal elements must have type " + elementType.describe() +
                            "; got " + actualType.describe() + ".");
                }
            }

            if (expectedArray) {
                const std::string expectedLength = expectedType->params[1].name;
                if (expectedLength != std::to_string(arrayExpr->elements.size())) {
                    reportError(
                        arrayExpr,
                        "Array literal has " + std::to_string(arrayExpr->elements.size()) +
                            " element(s), but the expected type is " + expectedType->describe() + ".");
                }
                type = *expectedType;
            } else {
                type.name = "Array";
                type.category = TypeCategory::Plain;
                type.params.push_back(elementType);
                ResolvedType length;
                length.name = std::to_string(std::max<size_t>(1, arrayExpr->elements.size()));
                length.category = TypeCategory::Plain;
                type.params.push_back(std::move(length));
            }
    } else if (auto* ifExpr = dynamic_cast<IfExpr*>(expr)) {
        const ResolvedType conditionType = analyzeExpr(ifExpr->condition.get());
        if (conditionType.isOpaqueExternal()) {
            reportError(ifExpr->condition.get(), opaqueExternalValueUseMessage(conditionType, "'if' condition"));
        } else if (!isConditionLike(conditionType)) {
            reportError(ifExpr->condition.get(), "'if' condition must be Bool, got " + conditionType.describe());
        }

        if (!ifExpr->elseBlock && valueRequired) {
            reportError(ifExpr, "'if' as a value requires an 'else' branch.");
        }

        if (!valueRequired) {
            enterSemanticScope();
            analyzeBlock(ifExpr->thenBlock.get());
            exitSemanticScope();
            if (ifExpr->elseBlock) {
                enterSemanticScope();
                analyzeBlock(ifExpr->elseBlock.get());
                exitSemanticScope();
            }
            type = makePlainType("Unit");
        } else {
            std::vector<ResolvedType> branchTypes;
            enterSemanticScope();
            branchTypes.push_back(analyzeBlockValue(ifExpr->thenBlock.get(), expectedType));
            exitSemanticScope();
            if (ifExpr->elseBlock) {
                enterSemanticScope();
                branchTypes.push_back(analyzeBlockValue(ifExpr->elseBlock.get(), expectedType));
                exitSemanticScope();
            }
            type = ifExpr->elseBlock
                ? unifyBranchTypes(ifExpr, branchTypes, expectedType, "if expression")
                : branchTypes.front();
            applyBlockTailType(ifExpr->thenBlock.get(), type);
            applyBlockTailType(ifExpr->elseBlock.get(), type);
            if (hasBorrowedStorageType(type) &&
                (blockTailReturnsBorrowFromOwnedTemporary(*this, ifExpr->thenBlock.get()) ||
                 blockTailReturnsBorrowFromOwnedTemporary(*this, ifExpr->elseBlock.get()))) {
                reportError(
                    ifExpr,
                    "Borrowed view from a temporary owner cannot cross an `if` expression branch. Hint: keep the owner in a local variable before the branch, or use the view only inside each branch.");
            }
        }
    } else if (auto* pickExpr = dynamic_cast<PickExpr*>(expr)) {
        const ResolvedType pickedType = analyzeExpr(pickExpr->value.get());
        if (pickedType.isOpaqueExternal()) {
            reportError(pickExpr->value.get(), opaqueExternalValueUseMessage(pickedType, "pick value"));
        }
        if (pickedType.name == "Tuple" && pickedType.isOwned() &&
            !dynamic_cast<IdentExpr*>(pickExpr->value.get())) {
            reportError(pickExpr->value.get(), "Pick over a temporary owned tuple is unsupported; bind it to a local first.");
        }
        const bool tupleValue = pickedType.name == "Tuple";

        const ChoiceInfo* choiceInfo = (pickedType.isUnknown() || pickedType.isOpaqueExternal())
            ? nullptr
            : lookupChoice(pickedType.name);
        if (valueRequired && !choiceInfo && !tupleValue) {
            reportError(pickExpr, "A pick expression requires a choice type to prove exhaustiveness.");
        }

        if (tupleValue) {
            bool tuplePatternSeen = false;
            bool wildcardSeen = false;
            std::vector<ResolvedType> branchTypes;
            for (size_t branchIndex = 0; branchIndex < pickExpr->branches.size(); ++branchIndex) {
                const auto& branch = pickExpr->branches[branchIndex];
                const bool isWildcard = branch.tag == "_";

                if (tuplePatternSeen) {
                    reportError(branch.span, "Unreachable pick branch after an irrefutable tuple pattern.");
                }
                if (wildcardSeen) {
                    if (isWildcard) {
                        reportError(branch.span, "Duplicate pick branch: _");
                    } else {
                        reportError(branch.span, "Unreachable pick branch after the '_' catch-all branch.");
                    }
                }

                if (isWildcard) {
                    if (branchIndex + 1 != pickExpr->branches.size()) {
                        reportError(branch.span, "The '_' catch-all branch must be the last branch in a pick.");
                    }
                    if (!branch.bindings.empty()) {
                        reportError(branch.span, "The '_' catch-all branch cannot bind payload values.");
                    }
                    wildcardSeen = true;
                }

                enterSemanticScope();
                if (branch.tuplePattern && !wildcardSeen) {
                    if (analyzeTuplePickPattern(branch.tuplePattern.get(), pickedType, "value")) {
                        bindTuplePickPattern(branch.tuplePattern.get(), pickedType);
                    } else {
                        for (const auto& binding : branch.bindings) {
                            defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                        }
                    }
                    tuplePatternSeen = true;
                } else if (branch.tuplePattern) {
                    for (const auto& binding : branch.bindings) {
                        defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                    }
                } else if (!isWildcard && !branch.tuplePattern && !wildcardSeen) {
                    reportError(branch.span, "A pick over a tuple must use a tuple pattern or the '_' catch-all.");
                }

                if (valueRequired) {
                    branchTypes.push_back(analyzeBlockValue(branch.body.get(), expectedType));
                } else {
                    analyzeBlock(branch.body.get());
                }
                exitSemanticScope();
            }

            if (!wildcardSeen && !tuplePatternSeen) {
                reportError(pickExpr, "Non-exhaustive pick for tuple type '" + pickedType.describe() + "'.");
            }
            if (valueRequired) {
                type = unifyBranchTypes(pickExpr, branchTypes, expectedType, "pick expression");
                for (const auto& branch : pickExpr->branches) {
                    applyBlockTailType(branch.body.get(), type);
                }
            } else {
                type = makePlainType("Unit");
            }
        } else {
        std::unordered_map<std::string, ResolvedType> choiceBindings;
        if (choiceInfo) {
            choiceBindings = buildTypeBindingsChecked(
                choiceInfo->typeParams,
                pickedType.params,
                "choice '" + pickedType.name + "'");
        }

        std::unordered_set<std::string> seenTags;
        bool wildcardSeen = false;
        std::vector<ResolvedType> branchTypes;
        for (size_t branchIndex = 0; branchIndex < pickExpr->branches.size(); ++branchIndex) {
            auto& branch = pickExpr->branches[branchIndex];
            const bool isWildcard = branch.tag == "_";
            if (isWildcard) {
                if (wildcardSeen) {
                    reportError(branch.span, "Duplicate pick branch: _");
                }
                if (branchIndex + 1 != pickExpr->branches.size()) {
                    reportError(branch.span, "The '_' catch-all branch must be the last branch in a pick.");
                }
                if (!branch.bindings.empty()) {
                    reportError(branch.span, "The '_' catch-all branch cannot bind payload values.");
                }
                wildcardSeen = true;
            } else if (wildcardSeen) {
                reportError(branch.span, "Unreachable pick branch after the '_' catch-all branch.");
            } else if (branch.tuplePattern) {
                // The non-tuple path below reports the value/pattern mismatch.
            } else if (!seenTags.insert(branch.tag).second) {
                reportError(branch.span, "Duplicate pick branch: " + branch.tag);
            }

            enterSemanticScope();
            if (branch.tuplePattern) {
                analyzeTuplePickPattern(branch.tuplePattern.get(), pickedType, "value");
                for (const auto& binding : branch.bindings) {
                    defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                }
            } else if (choiceInfo && isWildcard) {
                for (const auto& binding : branch.bindings) {
                    defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                }
            } else if (choiceInfo && !wildcardSeen) {
                const auto variantIt = choiceInfo->variants.find(branch.tag);
                if (variantIt == choiceInfo->variants.end()) {
                    reportError(branch.span, "Unknown choice variant '" + branch.tag + "' for type '" + pickedType.name + "'.");
                } else {
                    const auto& payloadTypes = variantIt->second.payloadTypes;
                    branch.bindingKeys.resize(branch.bindings.size());
                    if (payloadTypes.size() != branch.bindings.size()) {
                        reportError(
                            branch.span,
                            "Branch '" + branch.tag + "' expects " + std::to_string(payloadTypes.size()) +
                                " binding(s), got " + std::to_string(branch.bindings.size()));
                    }
                    for (size_t i = 0; i < branch.bindings.size(); ++i) {
                        const ResolvedType payloadType = i < payloadTypes.size()
                            ? substituteType(payloadTypes[i], choiceBindings)
                            : makeUnknownType();
                        defineVariable(
                            branch.bindings[i],
                            payloadType,
                            false,
                            "Duplicate pick binding: " + branch.bindings[i],
                            branch.span,
                            std::nullopt,
                            nullptr,
                            &branch.bindingKeys[i]);
                    }
                }
            } else {
                for (const auto& binding : branch.bindings) {
                    defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                }
            }

            if (valueRequired) {
                branchTypes.push_back(analyzeBlockValue(branch.body.get(), expectedType));
            } else {
                analyzeBlock(branch.body.get());
            }
            exitSemanticScope();
        }

        if (choiceInfo && !wildcardSeen && seenTags.size() != choiceInfo->variantOrder.size()) {
            reportError(pickExpr, "Non-exhaustive pick for choice type '" + pickedType.name + "'.");
        }

        if (valueRequired) {
            type = unifyBranchTypes(pickExpr, branchTypes, expectedType, "pick expression");
            for (const auto& branch : pickExpr->branches) {
                applyBlockTailType(branch.body.get(), type);
            }
        } else {
            type = makePlainType("Unit");
        }
        }
        if (valueRequired && hasBorrowedStorageType(type) &&
            std::any_of(pickExpr->branches.begin(), pickExpr->branches.end(), [&](const PickBranch& branch) {
                return blockTailReturnsBorrowFromOwnedTemporary(*this, branch.body.get());
            })) {
            reportError(
                pickExpr,
                "Borrowed view from a temporary owner cannot cross a `pick` expression branch. Hint: keep the owner in a local variable before the `pick`, or use the view only within each branch.");
        }
    } else if (auto* shapeInit = dynamic_cast<ShapeInitExpr*>(expr)) {
        const ShapeInfo* shapeInfo = lookupShape(shapeInit->name);
        if (!shapeInfo) {
            reportError(shapeInit, "Unknown shape constructor: " + shapeInit->name);
            analysisResult.exprTypes[expr] = type;
            return type;
        }

        ResolvedType concreteShapeType = makeOwnedType(shapeInit->name);
        std::unordered_map<std::string, ResolvedType> genericBindings;
        if (!shapeInfo->typeParams.empty()) {
            bool unresolvedExpectedType = false;
            if (expectedType) {
                std::vector<const ResolvedType*> pendingTypes{expectedType};
                while (!pendingTypes.empty()) {
                    const ResolvedType* current = pendingTypes.back();
                    pendingTypes.pop_back();
                    if (current->isGeneric || current->isUnknown() || current->isOpaqueExternal()) {
                        unresolvedExpectedType = true;
                        break;
                    }
                    for (const auto& param : current->params) {
                        pendingTypes.push_back(&param);
                    }
                }
            }
            if (!expectedType || expectedType->name != shapeInit->name ||
                expectedType->params.size() != shapeInfo->typeParams.size() || unresolvedExpectedType) {
                reportError(
                    shapeInit,
                    "Generic shape literal '" + shapeInit->name +
                        "' requires a concrete matching expected type, for example `" +
                        shapeInit->name + "[SomeType]`.");
                analysisResult.exprTypes[expr] = type;
                return type;
            }
            concreteShapeType = *expectedType;
            genericBindings = buildTypeBindingsChecked(
                shapeInfo->typeParams,
                concreteShapeType.params,
                "shape literal '" + shapeInit->name + "'");
        }

        if (!shapeInfo->isViewShape && !shapeInit->scopeName.empty()) {
            reportError(shapeInit, "Shape '" + shapeInit->name + "' is not a view shape and cannot take a named scope.");
        }
        if (!shapeInit->scopeName.empty() && !isNamedScopeActive(shapeInit->scopeName)) {
            reportError(shapeInit, "Named scope `" + shapeInit->scopeName + "` is not active here.");
        }

        std::unordered_set<std::string> seenFields;
        std::vector<std::pair<const ShapeInitField*, ResolvedType>> fieldValueTypes;
        fieldValueTypes.reserve(shapeInit->fields.size());
        std::string actualScopeName = shapeInit->scopeName;

        for (const auto& field : shapeInit->fields) {
            if (!seenFields.insert(field.name).second) {
                reportError(field.span, "Duplicate field in shape literal '" + shapeInit->name + "': " + field.name);
                continue;
            }

            const auto fieldIt = shapeInfo->fields.find(field.name);
            if (fieldIt == shapeInfo->fields.end()) {
                reportError(field.span, "Unknown field '" + field.name + "' in shape literal '" + shapeInit->name + "'.");
                continue;
            }

            ResolvedType expectedFieldType = substituteType(fieldIt->second, genericBindings);
            const ResolvedType valueType = analyzeExpr(field.value.get(), &expectedFieldType);
            fieldValueTypes.push_back({&field, valueType});

            if (shapeInfo->isViewShape && typeContainsBorrowedStorage(expectedFieldType)) {
                if (valueType.viewScope.empty()) {
                    reportError(
                        field.value.get(),
                        "Field '" + field.name + "' of view shape '" + shapeInit->name +
                            "' must be initialized from a named scoped borrow like ref[s] ...");
                } else if (actualScopeName.empty()) {
                    actualScopeName = valueType.viewScope;
                } else if (actualScopeName != valueType.viewScope) {
                    reportError(
                        field.value.get(),
                        "All borrowed fields of view shape '" + shapeInit->name +
                            "' must come from the same named scope. Saw `" + actualScopeName +
                            "` and `" + valueType.viewScope + "`.");
                }
            }
        }

        for (const auto& fieldName : shapeInfo->fieldOrder) {
            if (!seenFields.contains(fieldName)) {
                reportError(shapeInit, "Missing field '" + fieldName + "' in shape literal '" + shapeInit->name + "'.");
            }
        }

        if (shapeInfo->isViewShape && actualScopeName.empty()) {
            reportError(
                shapeInit,
                "View shape '" + shapeInit->name + "' requires borrowed fields tied to an active named scope. Use `scope s { ... }` and initialize with `ref[s] ...`.");
        }

        for (const auto& [field, valueType] : fieldValueTypes) {
            const auto fieldIt = shapeInfo->fields.find(field->name);
            if (fieldIt == shapeInfo->fields.end()) {
                continue;
            }
            ResolvedType expectedFieldType = substituteType(fieldIt->second, genericBindings);
            if (shapeInfo->isViewShape) {
                expectedFieldType = bindFormalScopeName(expectedFieldType, shapeInfo->scopeParamName, actualScopeName);
            }
            const bool matches = expectedFieldType.isView()
                ? canPassArgumentType(field->value.get(), valueType, expectedFieldType)
                : canAssignType(valueType, expectedFieldType);
            if (!valueType.isOpaqueExternal() && !matches) {
                reportError(
                    field->value.get(),
                    "Shape literal field type mismatch for '" + field->name + "': expected " +
                        expectedFieldType.describe() + ", got " + valueType.describe());
            }
        }

        type = concreteShapeType;
        if (shapeInfo->isViewShape) {
            type.scopeName = actualScopeName;
        }
    } else if (auto* ident = dynamic_cast<IdentExpr*>(expr)) {
        const ChoiceInfo* expectedChoiceInfo = nullptr;
        if (expectedType && !expectedType->isUnknown() && !expectedType->isOpaqueExternal()) {
            if (const auto* choiceInfo = lookupChoice(expectedType->name)) {
                expectedChoiceInfo = choiceInfo;
                if (const auto variantInfo = choice_utils::resolveChoiceVariantInfo(*choiceInfo, *expectedType, ident->name)) {
                    if (!variantInfo->payloadTypes.empty()) {
                        reportError(
                            ident,
                            "Constructor '" + ident->name + "' requires " +
                                std::to_string(variantInfo->payloadTypes.size()) + " argument(s). Use call syntax.");
                    } else {
                        ChoiceConstructorInfo constructorInfo;
                        constructorInfo.resultType = *expectedType;
                        constructorInfo.variantName = choice_utils::findChoiceVariantName(*choiceInfo, ident->name).value_or(ident->name);
                        analysisResult.choiceConstructors[ident] = std::move(constructorInfo);
                        type = *expectedType;
                        analysisResult.exprTypes[expr] = type;
                        return type;
                    }
                }
            }
        }

        ident->backendBindingKey.clear();
        const auto sym = lookupSymbol(ident->name);
        if (!sym) {
            if (expectedChoiceInfo) {
                reportError(
                    ident,
                    "Unknown choice variant '" + ident->name + "' for type '" + expectedType->name + "'.");
            } else {
                reportError(ident, "Undefined variable: " + ident->name);
            }
        } else if (sym->kind == SymbolKind::Variable || sym->kind == SymbolKind::Shape) {
            type = sym->type;
            ident->backendBindingKey = sym->backendBindingKey;
        } else {
            type = makeUnknownType(ident->name);
        }
    } else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        const ResolvedType objectType = analyzeExpr(index->object.get());
        const ResolvedType expectedIndexType = makePlainType("USize");
        const ResolvedType indexType = analyzeExpr(index->index.get(), &expectedIndexType);
        const bool strRangeIndex = objectType.name == "Str" && isInt32RangeType(indexType);
        if (objectType.name == "Array") {
            const auto* unaryIndex = dynamic_cast<const UnaryExpr*>(index->index.get());
            if (unaryIndex && unaryIndex->op == "-" && dynamic_cast<const IntExpr*>(unaryIndex->operand.get())) {
                reportError(index->index.get(), "Fixed Array index cannot be a negative integer literal.");
            }
        }

        if (objectType.isOpaqueExternal()) {
            reportError(index->object.get(), opaqueExternalValueUseMessage(objectType, "index receiver"));
        }
        if (indexType.isOpaqueExternal()) {
            reportError(index->index.get(), opaqueExternalValueUseMessage(indexType, "index value"));
        }
        if (!indexType.isUnknown() && !strRangeIndex && !canAssignType(indexType, expectedIndexType)) {
            reportError(index->index.get(), "Index expression must have type USize, got " + indexType.describe());
        }

        if (!objectType.isUnknown() && !objectType.isOpaqueExternal()) {
            if (objectType.name == "Str") {
                if (strRangeIndex) {
                    type = asViewType(makeOwnedType("Str"), "look");
                    type.viewScope = objectType.viewScope;
                } else {
                    type = makePlainType("UInt8");
                }
            } else if ((objectType.name == "Span" || objectType.name == "Vec") && !objectType.params.empty()) {
                type = objectType.params.front();
            } else if (isSupportedFixedArrayType(objectType)) {
                type = objectType.params.front();
            } else {
                reportError(index, "Type '" + objectType.describe() + "' does not support index access.");
            }
        }
    } else if (auto* borrow = dynamic_cast<BorrowExpr*>(expr)) {
        const ResolvedType targetType = analyzeExpr(borrow->target.get());
        const bool addressable =
            dynamic_cast<IdentExpr*>(borrow->target.get()) ||
            dynamic_cast<MemberExpr*>(borrow->target.get()) ||
            dynamic_cast<IndexExpr*>(borrow->target.get());

        if (!addressable) {
            reportError(borrow, "Borrow target must be an addressable expression.");
        }
        if (targetType.isOpaqueExternal()) {
            reportError(borrow->target.get(), opaqueExternalValueUseMessage(targetType, "borrow target"));
        }
        if (auto* index = dynamic_cast<IndexExpr*>(borrow->target.get())) {
            if (const auto* objectType = lookupExprType(index->object.get())) {
                if (objectType->name == "Vec") {
                    reportError(borrow, "Direct element borrows from Vec are forbidden. Borrow through `.span()` first.");
                }
            }
        }
        if (borrow->isMutable && !canBorrowExprAsEdit(borrow->target.get())) {
            reportError(borrow, "Mutable borrow requires mutable storage or ref mut access.");
        }
        if (!borrow->scopeName.empty() && !isNamedScopeActive(borrow->scopeName)) {
            reportError(borrow, "Named scope `" + borrow->scopeName + "` is not active here.");
        }
        if (!borrow->scopeName.empty()) {
            ResolvedType scopedType = targetType;
            scopedType.viewKind = "look";
            scopedType.viewScope = borrow->scopeName;
            scopedType.category = TypeCategory::View;
            validateScopedViewType(scopedType, borrow, "borrow expressions");
            validateScopedBorrowSource(borrow->target.get(), scopedType, borrow, "borrow expression");
        }

        type = targetType;
        type.viewKind = borrow->isMutable ? "edit" : "look";
        type.viewScope = borrow->scopeName;
        type.category = TypeCategory::View;
    } else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        const auto* negatedIntegerLiteral = dynamic_cast<IntExpr*>(unary->operand.get());
        const bool allowNegativeMagnitude = unary->op == "-" && negatedIntegerLiteral;
        const ResolvedType operandType = analyzeExpr(unary->operand.get(), expectedType, allowNegativeMagnitude);
        if (unary->op == "-") {
            if (operandType.isUnknown() || isNumericTypeName(operandType.name) || isNumericLiteralType(operandType)) {
                type = isNumericLiteralType(operandType) ? normalizeInferredLiteralType(operandType) : operandType;
                if (allowNegativeMagnitude && isIntegerLiteralType(operandType)) {
                    const auto literal = parseIntegerLiteralText(negatedIntegerLiteral->value);
                    if (literal.suffix.empty() && !shouldUseExpectedIntegerType(expectedType) && !shouldUseExpectedFloatType(expectedType)) {
                        if (!integerLiteralFitsNegatedTarget(literal.magnitude, "Int32", target)) {
                            reportError(
                                negatedIntegerLiteral,
                                "Integer literal '" + literal.magnitude + "' does not fit target type Int32.");
                        }
                        type = defaultIntegerLiteralType();
                        analysisResult.exprTypes[unary->operand.get()] = type;
                    }
                }
            } else {
                reportError(unary, "Unary operator '-' requires a numeric operand, got " + operandType.describe() + ".");
                type = makeUnknownType();
            }
        } else if (unary->op == "not") {
            if (!operandType.isUnknown() && operandType.name != "Bool") {
                reportError(unary, "Unary operator 'not' requires Bool, got " + operandType.describe() + ".");
                type = makeUnknownType();
            } else {
                type = makePlainType("Bool");
            }
        }
    } else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        if (binary->op == ".." || binary->op == "..=") {
            const ResolvedType int32Type = makePlainType("Int32");
            const ResolvedType charType = makePlainType("Char");
            const ResolvedType* endpointExpected = nullptr;
            if (expectedType && expectedType->name == "Range" && expectedType->params.size() == 1 &&
                isSupportedRangeElementType(expectedType->params.front())) {
                endpointExpected = &expectedType->params.front();
            }
            ResolvedType leftType = analyzeExpr(binary->left.get(), endpointExpected);
            ResolvedType rightType;
            bool rightAnalyzed = false;
            if (!endpointExpected) {
                if (isIntegerLiteralType(leftType)) {
                    rightType = analyzeExpr(binary->right.get());
                    rightAnalyzed = true;
                    if (isSupportedRangeElementType(rightType)) {
                        endpointExpected = &rightType;
                    } else if (isIntegerLiteralType(rightType)) {
                        endpointExpected = &int32Type;
                    }
                    leftType = analyzeExpr(binary->left.get(), endpointExpected);
                    if (isIntegerLiteralType(rightType) && endpointExpected) {
                        rightType = analyzeExpr(binary->right.get(), endpointExpected);
                    }
                } else if (isSupportedRangeElementType(leftType)) {
                    endpointExpected = &leftType;
                }
            }
            if (!rightAnalyzed) {
                rightType = analyzeExpr(binary->right.get(), endpointExpected);
            }
            if (isSupportedRangeElementType(leftType) && isSupportedRangeElementType(rightType) &&
                sameType(leftType, rightType)) {
                type = makePlainType("Range");
                type.params.push_back(leftType);
            } else {
                if (!leftType.isUnknown() && !rightType.isUnknown()) {
                    reportError(
                        binary,
                        "Range bounds currently support only Int8, Int16, Int32, Int64, Int128, UInt8, UInt16, UInt32, UInt64, UInt128, USize, or Char with matching types, got " +
                            leftType.describe() + " and " + rightType.describe() + ".");
                }
                type = makeUnknownType();
            }
            analysisResult.exprTypes[expr] = type;
            return type;
        }

        if (binary->op == "in") {
            const ResolvedType rightType = analyzeExpr(binary->right.get());
            const ResolvedType leftType = isSupportedRangeType(rightType)
                ? analyzeExpr(binary->left.get(), &rightType.params.front())
                : analyzeExpr(binary->left.get());
            if (isSupportedRangeType(rightType) && isSupportedRangeElementType(leftType) &&
                sameType(leftType, rightType.params.front())) {
                type = makePlainType("Bool");
            } else {
                if (!leftType.isUnknown() && !rightType.isUnknown()) {
                    reportError(
                        binary,
                        "Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair, got " +
                            leftType.describe() + " and " + rightType.describe() + ".");
                }
                type = makeUnknownType();
            }
            analysisResult.exprTypes[expr] = type;
            return type;
        }

        const bool isLogical = binary->op == "and" || binary->op == "or";
        const bool isComparison = binary->op == "==" || binary->op == "!=" || binary->op == "<" || binary->op == "<=" ||
            binary->op == ">" || binary->op == ">=";
        const ResolvedType* operandExpected = (expectedType && isConcreteNumericType(*expectedType)) ? expectedType : nullptr;
        ResolvedType leftType = analyzeExpr(binary->left.get(), operandExpected);
        ResolvedType rightType = analyzeExpr(binary->right.get(), operandExpected);

        const auto preferredType = numericLiteralContextType(operandExpected, leftType, rightType);
        if (preferredType.has_value()) {
            if (isNumericLiteralType(leftType)) {
                leftType = analyzeExpr(binary->left.get(), &*preferredType);
            }
            if (isNumericLiteralType(rightType)) {
                rightType = analyzeExpr(binary->right.get(), &*preferredType);
            }
        }

        if (leftType.isOpaqueExternal()) {
            reportError(binary->left.get(), opaqueExternalValueUseMessage(leftType, isComparison ? "comparison operand" : "arithmetic operand"));
        }
        if (rightType.isOpaqueExternal()) {
            reportError(binary->right.get(), opaqueExternalValueUseMessage(rightType, isComparison ? "comparison operand" : "arithmetic operand"));
        }

        auto getOpMethod = [](const std::string& op) -> std::string {
            if (op == "+") return "add";
            if (op == "-") return "sub";
            if (op == "*") return "mul";
            if (op == "/") return "div";
            if (op == "==" || op == "!=") return "equal";
            if (op == "<" || op == ">" || op == "<=" || op == ">=") return "compare";
            return "";
        };

        std::string opMethod = getOpMethod(binary->op);
        const FunctionSignature* opMethodSig = nullptr;
        if (!opMethod.empty() && !leftType.isUnknown()) {
            opMethodSig = lookupFunctionSignature(leftType.name + "." + opMethod);
        }

        if (isLogical) {
            if (!leftType.isUnknown() && leftType.name != "Bool") {
                reportError(binary->left.get(), "Logical operator '" + binary->op + "' requires Bool operands, got " + leftType.describe() + ".");
            }
            if (!rightType.isUnknown() && rightType.name != "Bool") {
                reportError(binary->right.get(), "Logical operator '" + binary->op + "' requires Bool operands, got " + rightType.describe() + ".");
            }
            type = makePlainType("Bool");
        } else if (leftType.isOpaqueExternal() || rightType.isOpaqueExternal()) {
            type = makeUnknownType();
        } else if (opMethodSig != nullptr) {
            if (isComparison) {
                type = makePlainType("Bool");
            } else {
                type = opMethodSig->returnType;
            }
        } else if (isComparison && (leftType.name == "Range" || rightType.name == "Range")) {
            reportError(binary, "Range values support `in` membership only; range comparisons are not supported.");
            type = makeUnknownType();
        } else if (isComparison) {
            if (!leftType.isUnknown() && !rightType.isUnknown() &&
                !canAssignType(rightType, leftType) && !canAssignType(leftType, rightType) &&
                !canCompareTypes(leftType, rightType)) {
                reportError(binary, "Incompatible comparison operands: " + leftType.describe() + " and " + rightType.describe());
            }
            type = makePlainType("Bool");
        } else if (!leftType.isUnknown() && !rightType.isUnknown() && binary->op == "%") {
            const bool integerOperands = sameType(leftType, rightType) &&
                (isIntegerLikeTypeName(leftType.name) || isIntegerLiteralType(leftType)) &&
                (isIntegerLikeTypeName(rightType.name) || isIntegerLiteralType(rightType));
            if (integerOperands) {
                type = leftType;
            } else {
                reportError(
                    binary,
                    "Modulo operator '%' requires integer operands of the same type, got " +
                        leftType.describe() + " and " + rightType.describe() + ".");
                type = makeUnknownType();
            }
        } else if (!leftType.isUnknown() && !rightType.isUnknown()) {
            if (sameType(leftType, rightType) && isNumericTypeName(leftType.name)) {
                type = leftType;
            } else {
                reportError(binary, "Incompatible arithmetic operands: " + leftType.describe() + " and " + rightType.describe());
                type = makeUnknownType();
            }
        }
    } else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        const FunctionSignature* signature = nullptr;
        std::optional<MethodSignature> methodSignature;
        bool externalCall = false;
        bool opaqueExternalCall = false;
        bool skipCalleeAnalysis = false;
        std::string opaqueExternalCallee;

        if (isStringFromCall(call->callee.get())) {
            const ResolvedType expectedView = makeStrViewType();
            if (call->args.size() != 1) {
                reportError(call, "String.from(view) expects exactly one Str view argument. Hint: pass a string literal or call `owner.as_str()` to copy owned text.");
            }
            for (size_t i = 0; i < call->args.size(); ++i) {
                const ResolvedType argumentType = analyzeExpr(call->args[i].get(), &expectedView);
                if (i == 0 && !canPassArgumentType(call->args[i].get(), argumentType, expectedView)) {
                    reportError(
                        call->args[i].get(),
                        "String.from(view) requires Str, got " + argumentType.describe() +
                            ". Hint: pass a string literal or borrowed `Str`; when the source is a `String`, call `owner.as_str()`.");
                }
            }
            type = makeOwnedType("String");
            analysisResult.exprTypes[expr] = type;
            return type;
        }

        if (isArenaNewCall(call->callee.get())) {
            if (!call->args.empty()) {
                reportError(call, "Arena.new() expects exactly 0 arguments.");
            }
            type = makeOwnedType("Arena");
            analysisResult.exprTypes[expr] = type;
            return type;
        }

        if (isAnchorStaticConstructorCall(call->callee.get())) {
            const ResolvedType* expectedPayloadType = nullptr;
            if (expectedType && !expectedType->isUnknown() && !expectedType->isOpaqueExternal() &&
                expectedType->name == "Anchor" && expectedType->params.size() == 1) {
                expectedPayloadType = &expectedType->params.front();
            }

            bool canValidatePayloadGraph = false;
            if (call->args.size() != 1) {
                reportError(call, "Anchor.new(...) expects exactly 1 argument.");
            }

            ResolvedType payloadType = makeUnknownType();
            if (!call->args.empty()) {
                payloadType = analyzeExpr(call->args.front().get(), expectedPayloadType);
                if (payloadType.isOpaqueExternal()) {
                    reportError(call->args.front().get(), opaqueExternalValueUseMessage(payloadType, "Anchor payload"));
                }
                if (rawDepth == 0 && isRawAddressType(payloadType)) {
                    reportError(call->args.front().get(), "Raw address values may only appear inside raw blocks.");
                }
                if (!payloadType.isUnknown() && !payloadType.isOpaqueExternal()) {
                    canValidatePayloadGraph = isAnchorPayloadTypeAllowed(payloadType);
                    if (!canValidatePayloadGraph) {
                        reportError(call->args.front().get(), anchorPayloadTypeError(payloadType));
                    }
                }
                if (expectedPayloadType && !payloadType.isOpaqueExternal() &&
                    !canPassArgumentType(call->args.front().get(), payloadType, *expectedPayloadType)) {
                    reportError(
                        call->args.front().get(),
                        "Anchor payload type mismatch: expected " + expectedPayloadType->describe() +
                            ", got " + payloadType.describe());
                }
            }

            if (expectedType && !expectedType->isUnknown() && !expectedType->isOpaqueExternal() &&
                expectedType->name == "Anchor" && expectedType->params.size() == 1) {
                type = *expectedType;
            } else {
                type = makeOwnedType("Anchor");
                if (isIntegerLiteralType(payloadType)) {
                    validateInferredIntegerLiteral(call->args.front().get(), "Int32");
                }
                type.params.push_back(normalizeInferredLiteralType(payloadType));
            }
            if (canValidatePayloadGraph && !validateAnchorPayloadType(type, call, "Anchor.new payloads")) {
                type = makeUnknownType();
            }
            analysisResult.exprTypes[expr] = type;
            return type;
        }

        if (auto* calleeIdent = dynamic_cast<IdentExpr*>(call->callee.get())) {
            if (expectedType && !expectedType->isUnknown() && !expectedType->isOpaqueExternal()) {
                if (const auto* choiceInfo = lookupChoice(expectedType->name)) {
                    if (const auto variantInfo = choice_utils::resolveChoiceVariantInfo(*choiceInfo, *expectedType, calleeIdent->name)) {
                        if (call->args.size() != variantInfo->payloadTypes.size()) {
                            reportError(
                                call,
                                "Constructor '" + calleeIdent->name + "' expects " +
                                    std::to_string(variantInfo->payloadTypes.size()) + " argument(s), got " +
                                    std::to_string(call->args.size()));
                        }

                        for (size_t i = 0; i < call->args.size(); ++i) {
                            const ResolvedType* payloadType = i < variantInfo->payloadTypes.size()
                                ? &variantInfo->payloadTypes[i]
                                : nullptr;
                            const ResolvedType argType = analyzeExpr(call->args[i].get(), payloadType);
                            if (argType.isOpaqueExternal()) {
                                reportError(call->args[i].get(), opaqueExternalValueUseMessage(argType, "constructor argument"));
                                continue;
                            }
                            if (payloadType && !canPassArgumentType(call->args[i].get(), argType, *payloadType)) {
                                reportError(
                                    call->args[i].get(),
                                    "Constructor argument type mismatch: expected " + payloadType->describe() +
                                        ", got " + argType.describe() + stringOwnershipHint(*payloadType, argType));
                            }
                        }

                        ChoiceConstructorInfo constructorInfo;
                        constructorInfo.resultType = *expectedType;
                        constructorInfo.variantName = choice_utils::findChoiceVariantName(*choiceInfo, calleeIdent->name).value_or(calleeIdent->name);
                        constructorInfo.payloadTypes = variantInfo->payloadTypes;
                        analysisResult.choiceConstructors[call] = std::move(constructorInfo);
                        type = *expectedType;
                        analysisResult.exprTypes[expr] = type;
                        return type;
                    }
                }
            }

            signature = lookupFunctionSignature(calleeIdent->name);
            if (!signature) {
                const auto sym = lookupSymbol(calleeIdent->name);
                if (sym && sym->kind == SymbolKind::Function && sym->isExternal) {
                    externalCall = true;
                    opaqueExternalCall = true;
                    opaqueExternalCallee = calleeIdent->name;
                }
            }
        } else if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
            if (auto* objectIdent = dynamic_cast<IdentExpr*>(member->object.get())) {
                const auto sym = lookupSymbol(objectIdent->name);
                if (sym && sym->kind == SymbolKind::Module) {
                    if (sym->moduleInfo) {
                        const auto exported = sym->moduleInfo->exportedItems.find(member->member);
                        if (exported != sym->moduleInfo->exportedItems.end()) {
                            if (exported->second.kind == SymbolKind::Function && exported->second.functionSignature.has_value()) {
                                signature = &*exported->second.functionSignature;
                            } else if (exported->second.kind != SymbolKind::Function) {
                                reportError(call, "Module member is not callable: " + objectIdent->name + "." + member->member);
                                skipCalleeAnalysis = true;
                            } else {
                                externalCall = true;
                                opaqueExternalCall = true;
                                opaqueExternalCallee = describeCalleeExpr(call->callee.get());
                            }
                        } else {
                            reportError(call, "Module '" + objectIdent->name + "' does not expose callable member '" + member->member + "'.");
                            skipCalleeAnalysis = true;
                        }
                    } else {
                        externalCall = true;
                        opaqueExternalCall = true;
                        opaqueExternalCallee = describeCalleeExpr(call->callee.get());
                    }
                } else if (sym && (sym->kind == SymbolKind::Shape || sym->kind == SymbolKind::Choice)) {
                    const std::string qualifiedName = objectIdent->name + "." + member->member;
                    signature = lookupFunctionSignature(qualifiedName);
                    if (signature) {
                        skipCalleeAnalysis = true;
                    }
                }
            }

            if (!signature && !externalCall && !skipCalleeAnalysis) {
                if (auto* objectIdent = dynamic_cast<IdentExpr*>(member->object.get())) {
                    if (objectIdent->name == "Anchor" && member->member == "new") {
                        if (call->args.size() != 1) {
                            reportError(call, "Constructor 'Anchor.new' expects exactly 1 argument.");
                        } else {
                            const ResolvedType* payloadExpectedType =
                                (expectedType && expectedType->name == "Anchor" && expectedType->params.size() == 1)
                                    ? &expectedType->params.front()
                                    : nullptr;
                            ResolvedType payloadType = analyzeExpr(call->args[0].get(), payloadExpectedType);
                            if (isIntegerLiteralType(payloadType)) {
                                validateInferredIntegerLiteral(call->args[0].get(), "Int32");
                            }
                            payloadType = normalizeInferredLiteralType(payloadType);
                            if (isNumericLiteralType(payloadType)) {
                                analysisResult.exprTypes[call->args[0].get()] = payloadType;
                            }

                            ResolvedType anchorType = makeOwnedType("Anchor");
                            if (payloadExpectedType) {
                                anchorType.params.push_back(*payloadExpectedType);
                                if (!canAssignType(payloadType, *payloadExpectedType)) {
                                    reportError(
                                        call->args[0].get(),
                                        "Anchor.new payload type mismatch: expected " + payloadExpectedType->describe() +
                                            ", got " + payloadType.describe());
                                }
                            } else {
                                anchorType.params.push_back(payloadType);
                            }

                            if (!validateAnchorPayloadType(anchorType, call, "Anchor.new payloads")) {
                                type = makeUnknownType();
                            } else {
                                type = anchorType;
                            }
                            analysisResult.exprTypes[expr] = type;
                            return type;
                        }
                    }
                }

                const ResolvedType receiverType = analyzeExpr(member->object.get());
                if (receiverType.isOpaqueExternal()) {
                    reportError(member->object.get(), opaqueExternalValueUseMessage(receiverType, "method receiver"));
                    skipCalleeAnalysis = true;
                } else {
                    methodSignature = lookupMethodSignature(receiverType, member->member);
                    if (methodSignature.has_value()) {
                        if (receiverType.name == "Arena" && member->member == "alloc") {
                            ResolvedType payloadType = makeUnknownType();
                            if (call->args.size() == 1) {
                                payloadType = analyzeExpr(call->args.front().get(), nullptr);
                            } else {
                                reportError(call, "Arena.alloc(...) expects exactly 1 argument.");
                            }
                            methodSignature->function.paramTypes.push_back(payloadType);
                            methodSignature->function.returnType = asViewType(payloadType, "look");
                            signature = &methodSignature->function;
                        } else {
                            signature = &methodSignature->function;
                        }
                        
                        if (!canPassArgumentType(member->object.get(), receiverType, methodSignature->receiverType)) {
                            reportError(
                                member->object.get(),
                                "Method receiver type mismatch: expected " + methodSignature->receiverType.describe() +
                                    ", got " + receiverType.describe());
                        }
                    } else if (!receiverType.isUnknown() && !lookupShape(receiverType.name)) {
                        reportError(call, "Type '" + receiverType.name + "' does not provide method '" + member->member + "'.");
                        skipCalleeAnalysis = true;
                    }
                }
            }
        }

        if (!methodSignature.has_value() && !skipCalleeAnalysis && !opaqueExternalCall) {
            analyzeExpr(call->callee.get());
        }

        if (opaqueExternalCall && rawDepth == 0) {
            reportError(call->callee.get(), opaqueExternalRawRequirementMessage(call->callee.get()));
        }
        if (signature && signature->externalInfo.has_value() && signature->externalInfo->rawOnly && rawDepth == 0) {
            reportError(call->callee.get(), typedExternalRawRequirementMessage(call->callee.get()));
        }

        if (signature) {
            if (call->args.size() != signature->paramTypes.size()) {
                if (!signature->isExternal || signature->paramTypes.size() != 1) {
                    const std::string calleeName = dynamic_cast<IdentExpr*>(call->callee.get())
                        ? dynamic_cast<IdentExpr*>(call->callee.get())->name
                        : describeCalleeExpr(call->callee.get());
                    reportError(
                        call,
                        "Call to '" + calleeName + "' expects " +
                            std::to_string(signature->paramTypes.size()) + " argument(s), got " +
                            std::to_string(call->args.size()));
                }
            }

            std::unordered_map<std::string, std::string> scopeBindings;
            for (size_t i = 0; i < call->args.size(); ++i) {
                const ResolvedType* paramType = i < signature->paramTypes.size() ? &signature->paramTypes[i] : nullptr;
                const ResolvedType argType = analyzeExpr(call->args[i].get(), paramType);
                if (argType.isOpaqueExternal()) {
                    reportError(call->args[i].get(), opaqueExternalValueUseMessage(argType, "call argument"));
                    continue;
                }
                if (rawDepth == 0 &&
                    ((paramType && isRawAddressType(*paramType)) || isRawAddressType(argType))) {
                    reportError(
                        call->args[i].get(),
                        "Raw address values may only cross call boundaries inside raw blocks.");
                }
                const bool scopesBound = !paramType ||
                    bindSignatureScopes(*paramType, argType, *signature, scopeBindings, call->args[i].get());
                const ResolvedType instantiatedParam = paramType
                    ? instantiateSignatureType(*paramType, scopeBindings)
                    : makeUnknownType();
                if (paramType && scopesBound && !canPassArgumentType(call->args[i].get(), argType, instantiatedParam)) {
                    reportError(
                        call->args[i].get(),
                        "Call argument type mismatch: expected " + instantiatedParam.describe() +
                            ", got " + argType.describe() + stringOwnershipHint(instantiatedParam, argType));
                }
            }

            std::vector<std::string> returnScopes;
            collectNamedScopeNames(signature->returnType, returnScopes);
            for (const auto& scopeName : returnScopes) {
                if (std::find(signature->scopeParams.begin(), signature->scopeParams.end(), scopeName) != signature->scopeParams.end() &&
                    scopeBindings.find(scopeName) == scopeBindings.end()) {
                    reportError(call, "Could not infer scope parameter '" + scopeName + "' for the return value.");
                }
            }

            type = instantiateSignatureType(signature->returnType, scopeBindings);
            if (type.isView() && type.viewScope.empty() && signature->viewReturnSourceParam.has_value()) {
                const size_t sourceIndex = *signature->viewReturnSourceParam;
                if (sourceIndex < call->args.size()) {
                    if (const auto* sourceType = lookupExprType(call->args[sourceIndex].get())) {
                        type.viewScope = sourceType->viewScope;
                    }
                }
            }
            if (type.isView() && methodSignature.has_value()) {
                if (methodSignature->viewReturnFromReceiver) {
                    if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
                        if (const auto* receiverType = lookupExprType(member->object.get())) {
                            if (type.viewScope.empty()) {
                                type.viewScope = receiverType->viewScope;
                            }
                        }
                    }
                } else if (methodSignature->viewReturnSourceArg.has_value()) {
                    const size_t sourceIndex = *methodSignature->viewReturnSourceArg;
                    if (sourceIndex < call->args.size()) {
                        if (const auto* sourceType = lookupExprType(call->args[sourceIndex].get())) {
                            if (type.viewScope.empty()) {
                                type.viewScope = sourceType->viewScope;
                            }
                        }
                    }
                }
            }
            if (isBorrowedStrMaybeType(type)) {
                const ResolvedType* sourceType = nullptr;
                if (methodSignature.has_value() && methodSignature->viewReturnFromReceiver) {
                    if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
                        sourceType = lookupExprType(member->object.get());
                    }
                } else if (signature->viewReturnSourceParam.has_value()) {
                    const size_t sourceIndex = *signature->viewReturnSourceParam;
                    if (sourceIndex < call->args.size()) {
                        sourceType = lookupExprType(call->args[sourceIndex].get());
                    }
                }
                if (sourceType) {
                    inheritMaybeStrViewScope(type, *sourceType);
                }
            }
        } else {
            if (!externalCall) {
                if (auto* calleeIdent = dynamic_cast<IdentExpr*>(call->callee.get())) {
                    const auto sym = lookupSymbol(calleeIdent->name);
                    if (sym && sym->kind == SymbolKind::Variable) {
                        reportError(calleeIdent, "Expression is not callable: " + calleeIdent->name);
                    }
                }
            }

            for (auto& arg : call->args) {
                const ResolvedType argType = analyzeExpr(arg.get());
                if (argType.isOpaqueExternal()) {
                    reportError(arg.get(), opaqueExternalValueUseMessage(argType, "call argument"));
                }
            }

            if (opaqueExternalCall) {
                type = opaqueExternalCallee.empty()
                    ? makeOpaqueExternalCallType(call->callee.get())
                    : makeOpaqueExternalType(opaqueExternalCallee);
            }
        }
    } else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        const ResolvedType objectType = analyzeExpr(member->object.get());

        if (auto* objectIdent = dynamic_cast<IdentExpr*>(member->object.get())) {
            const auto sym = lookupSymbol(objectIdent->name);
            if (sym && sym->kind == SymbolKind::Module) {
                if (sym->moduleInfo) {
                    const auto exported = sym->moduleInfo->exportedItems.find(member->member);
                    if (exported != sym->moduleInfo->exportedItems.end()) {
                        if (exported->second.kind == SymbolKind::Shape || exported->second.kind == SymbolKind::Choice) {
                            type.name = exported->second.name;
                            type.category = TypeCategory::Owned;
                        } else {
                            type = makeUnknownType(member->member);
                        }
                    } else {
                        type = makeUnknownType(member->member);
                    }
                } else {
                    type = makeUnknownType(member->member);
                }
                analysisResult.exprTypes[expr] = type;
                return type;
            }
        }

        if (!objectType.isUnknown()) {
            if (objectType.isOpaqueExternal()) {
                reportError(member->object.get(), opaqueExternalValueUseMessage(objectType, "member access"));
            } else if (lookupMethodSignature(objectType, member->member).has_value()) {
                type = makeUnknownType(member->member);
            } else if (objectType.name == "Tuple") {
                const auto index = parseTupleProjectionIndex(member->member);
                if (!index.has_value()) {
                    reportError(member, "Tuple projection requires a numeric index such as `.0`.");
                } else if (*index >= objectType.params.size()) {
                    reportError(
                        member,
                        "Tuple projection index " + std::to_string(*index) + " is out of range for a tuple with " +
                            std::to_string(objectType.params.size()) + " element(s).");
                } else {
                    member->member = std::to_string(*index);
                    type = adaptMemberType(objectType, objectType.params[*index]);
                }
            } else {
                const ShapeInfo* shape = lookupShape(objectType.name);
                if (!shape) {
                    reportError(member, "Type '" + objectType.name + "' does not expose fields.");
                } else {
                    const auto fieldIt = shape->fields.find(member->member);
                    if (fieldIt == shape->fields.end()) {
                        reportError(member, "Unknown field '" + member->member + "' on type '" + objectType.name + "'.");
                    } else {
                        ResolvedType fieldType = instantiateGenericType(
                            fieldIt->second,
                            shape->typeParams,
                            objectType.params,
                            "shape '" + objectType.name + "'");
                        if (shape->isViewShape) {
                            fieldType = bindFormalScopeName(fieldType, shape->scopeParamName, objectType.scopeName);
                        }
                        type = adaptMemberType(objectType, fieldType);
                    }
                }
            }
        }
    }

    analysisResult.exprTypes[expr] = type;
    return type;
}

std::shared_ptr<Symbol> SemanticAnalyzer::lookupSymbol(const std::string& name) const {
    return const_cast<ScopeTree&>(scopes).lookup(name);
}

std::optional<size_t> SemanticAnalyzer::resolveViewSourceParam(const Expr* expr) const {
    if (!expr) {
        return std::nullopt;
    }

    if (dynamic_cast<const StringExpr*>(expr)) {
        return std::optional<size_t>(SIZE_MAX);
    }

    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        const auto sym = lookupSymbol(ident->name);
        if (sym && sym->kind == SymbolKind::Variable) {
            if (sym->isStatic) {
                return std::optional<size_t>(SIZE_MAX);
            }
            return sym->viewSourceParamIndex;
        }
        return std::nullopt;
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return resolveViewSourceParam(member->object.get());
    }

    if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return resolveViewSourceParam(index->object.get());
    }

    if (auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
        return resolveViewSourceParam(borrow->target.get());
    }

    if (auto* ifExpr = dynamic_cast<const IfExpr*>(expr)) {
        const auto tail = [&](const BlockStmt* block) -> const Expr* {
            if (!block || block->statements.empty()) return nullptr;
            const auto* stmt = dynamic_cast<const ExprStmt*>(block->statements.back().get());
            return stmt ? stmt->expr.get() : nullptr;
        };
        const auto a = resolveViewSourceParam(tail(ifExpr->thenBlock.get()));
        const auto b = resolveViewSourceParam(tail(ifExpr->elseBlock.get()));
        return a.has_value() && b.has_value() && a == b ? a : std::nullopt;
    }
    if (auto* pickExpr = dynamic_cast<const PickExpr*>(expr)) {
        std::optional<size_t> common;
        for (const auto& branch : pickExpr->branches) {
            if (!branch.body || branch.body->statements.empty()) return std::nullopt;
            const auto* stmt = dynamic_cast<const ExprStmt*>(branch.body->statements.back().get());
            const auto source = stmt ? resolveViewSourceParam(stmt->expr.get()) : std::nullopt;
            if (!source.has_value()) return std::nullopt;
            if (!common.has_value()) common = source; else if (*common != *source) return std::nullopt;
        }
        return common;
    }
    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        const FunctionSignature* signature = nullptr;
        std::optional<MethodSignature> methodSignature;

        if (auto* calleeIdent = dynamic_cast<const IdentExpr*>(call->callee.get())) {
            signature = lookupFunctionSignature(calleeIdent->name);
        } else if (auto* member = dynamic_cast<const MemberExpr*>(call->callee.get())) {
            if (auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get())) {
                const auto sym = lookupSymbol(objectIdent->name);
                if (sym && sym->kind == SymbolKind::Module && sym->moduleInfo) {
                    const auto exported = sym->moduleInfo->exportedItems.find(member->member);
                    if (exported != sym->moduleInfo->exportedItems.end() &&
                        exported->second.kind == SymbolKind::Function &&
                        exported->second.functionSignature.has_value()) {
                        signature = &*exported->second.functionSignature;
                    }
                }
            }

            if (!signature) {
                methodSignature = lookupMethodSignature(call->callee.get());
                if (methodSignature.has_value()) {
                    if (methodSignature->viewReturnFromReceiver) {
                        return resolveViewSourceParam(member->object.get());
                    }
                    if (methodSignature->viewReturnSourceArg.has_value()) {
                        const size_t sourceIndex = *methodSignature->viewReturnSourceArg;
                        if (sourceIndex < call->args.size()) {
                            return resolveViewSourceParam(call->args[sourceIndex].get());
                        }
                    }
                }
            }
        }

        if (!signature) {
            return std::nullopt;
        }

        const std::optional<size_t> sourceParam =
            (signature->returnType.isView() || isBorrowedStrMaybeType(signature->returnType))
            ? signature->viewReturnSourceParam
            : (typeContainsScopeName(signature->returnType) ? signature->scopeReturnSourceParam : std::nullopt);
        if (!sourceParam.has_value()) {
            return std::nullopt;
        }
        const size_t sourceIndex = *sourceParam;
        if (sourceIndex >= call->args.size()) {
            return std::nullopt;
        }
        return resolveViewSourceParam(call->args[sourceIndex].get());
    }

    return std::nullopt;
}

bool SemanticAnalyzer::canBorrowAsView(const ResolvedType& from, const ResolvedType& to) const {
    if (!to.isView()) {
        return false;
    }

    if (from.isOpaqueExternal() || to.isOpaqueExternal()) {
        return false;
    }

    if (from.isUnknown() || to.isUnknown()) {
        return true;
    }

    if (!(from.isOwned() || from.isView())) {
        return false;
    }

    if (from.name != to.name || from.params.size() != to.params.size()) {
        return false;
    }
    if (!to.scopeName.empty() && !from.scopeName.empty() && from.scopeName != to.scopeName) {
        return false;
    }

    for (size_t i = 0; i < from.params.size(); ++i) {
        if (!sameType(from.params[i], to.params[i])) {
            return false;
        }
    }

    if (!to.viewScope.empty()) {
        if (from.isView() && !from.viewScope.empty() && from.viewScope != to.viewScope) {
            return false;
        }
    } else if (from.isView() && !from.viewScope.empty()) {
        return to.viewKind == "look";
    }

    if (from.isOwned()) {
        return true;
    }

    if (from.viewKind == to.viewKind) {
        return true;
    }

    return from.viewKind == "edit" && to.viewKind == "look";
}

bool SemanticAnalyzer::canPassArgumentType(Expr* expr, const ResolvedType& from, const ResolvedType& to) const {
    if (from.isOpaqueExternal() || to.isOpaqueExternal()) {
        return false;
    }

    if (canAssignType(from, to)) {
        return true;
    }

    if (from.isView() && !from.viewScope.empty() && to.isView() && to.viewScope.empty() && to.viewKind == "look") {
        return true;
    }

    if (!to.isView() || !canBorrowAsView(from, to)) {
        return false;
    }

    if (to.viewKind != "edit") {
        return true;
    }

    if (from.isView()) {
        return from.viewKind == "edit";
    }

    return canBorrowExprAsEdit(expr);
}

bool SemanticAnalyzer::canBorrowExprAsEdit(const Expr* expr) const {
    if (!expr) {
        return false;
    }

    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        const auto sym = lookupSymbol(ident->name);
        if (!sym || sym->kind != SymbolKind::Variable) {
            return false;
        }
        if (sym->type.isView()) {
            return sym->type.viewKind == "edit";
        }
        return sym->isMutable && sym->type.isOwned();
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        if (const auto* objectType = lookupExprType(member->object.get())) {
            if (objectType->isView()) {
                return objectType->viewKind == "edit";
            }
        }

        if (auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get())) {
            const auto sym = lookupSymbol(objectIdent->name);
            if (sym && sym->kind == SymbolKind::Variable && sym->isMutable && sym->type.isOwned()) {
                return true;
            }
        }

        return canBorrowExprAsEdit(member->object.get());
    }

    if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        if (const auto* objectType = lookupExprType(index->object.get())) {
            if (objectType->name == "Vec") {
                return false;
            }
            if (objectType->isView()) {
                return objectType->viewKind == "edit";
            }
        }

        if (auto* objectIdent = dynamic_cast<const IdentExpr*>(index->object.get())) {
            const auto sym = lookupSymbol(objectIdent->name);
            if (sym && sym->kind == SymbolKind::Variable && sym->isMutable && sym->type.name == "Array") {
                return true;
            }
        }

        return canBorrowExprAsEdit(index->object.get());
    }

    if (auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
        return borrow->isMutable && canBorrowExprAsEdit(borrow->target.get());
    }

    return false;
}

bool SemanticAnalyzer::isRawAddressType(const ResolvedType& type) const {
    return isRawAddressTypeName(type.name);
}

void SemanticAnalyzer::defineVariable(
    const std::string& name,
    const ResolvedType& type,
    bool isMutable,
    const std::string& duplicateMessage,
    const SourceSpan& duplicateSpan,
    std::optional<size_t> viewSourceParamIndex,
    BindingStmt* bindingDecl,
    std::string* backendBindingKeyOut) {
    auto sym = std::make_shared<Symbol>();
    sym->name = name;
    sym->kind = SymbolKind::Variable;
    sym->isMutable = isMutable;
    sym->type = type;
    sym->bindingDecl = bindingDecl;
    sym->viewSourceParamIndex = viewSourceParamIndex;
    sym->declaredNamedScopes = activeNamedScopes;
    sym->lexicalDepth = lexicalScopeDepth;

    if ((bindingDecl != nullptr || backendBindingKeyOut != nullptr) && scopes.lookup(name) != nullptr) {
        sym->backendBindingKey = "$local.binding." + std::to_string(nextLocalBindingIdentity++);
        if (bindingDecl != nullptr) {
            if (!bindingDecl->tuplePattern) {
                bindingDecl->backendBindingKey = sym->backendBindingKey;
            }
        }
    }

    if (backendBindingKeyOut != nullptr) {
        *backendBindingKeyOut = sym->backendBindingKey;
    }

    if (!scopes.define(name, sym)) {
        reportError(duplicateSpan, duplicateMessage);
    }
}

bool SemanticAnalyzer::isConditionLike(const ResolvedType& type) const {
    return type.isUnknown() || (type.isPlain() && type.name == "Bool");
}

bool SemanticAnalyzer::blockDefinitelyTerminates(const BlockStmt* block) const {
    for (const auto& stmt : block->statements) {
        if (stmtDefinitelyTerminates(stmt.get())) {
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::stmtDefinitelyTerminates(const Stmt* stmt) const {
    if (dynamic_cast<const ReturnStmt*>(stmt) || dynamic_cast<const StopStmt*>(stmt) || dynamic_cast<const SkipStmt*>(stmt)) {
        return true;
    }

    if (const auto* binding = dynamic_cast<const BindingStmt*>(stmt)) {
        const auto* valueType = binding->value ? lookupExprType(binding->value.get()) : nullptr;
        return valueType && valueType->name == "Never";
    }

    if (const auto* assignment = dynamic_cast<const AssignStmt*>(stmt)) {
        const auto* valueType = assignment->value ? lookupExprType(assignment->value.get()) : nullptr;
        return valueType && valueType->name == "Never";
    }

    if (auto* exprStmt = dynamic_cast<const ExprStmt*>(stmt)) {
        if (auto* ifExpr = dynamic_cast<const IfExpr*>(exprStmt->expr.get())) {
            return ifExpr->elseBlock && blockDefinitelyTerminates(ifExpr->thenBlock.get()) &&
                   blockDefinitelyTerminates(ifExpr->elseBlock.get());
        }
        if (auto* pickExpr = dynamic_cast<const PickExpr*>(exprStmt->expr.get())) {
            return !pickExpr->branches.empty() && std::all_of(
                pickExpr->branches.begin(),
                pickExpr->branches.end(),
                [&](const PickBranch& branch) { return blockDefinitelyTerminates(branch.body.get()); });
        }
        const auto* exprType = lookupExprType(exprStmt->expr.get());
        return exprType && exprType->name == "Never";
    }

    if (auto* ifStmt = dynamic_cast<const IfStmt*>(stmt)) {
        return ifStmt->elseBlock && blockDefinitelyTerminates(ifStmt->thenBlock.get()) &&
               blockDefinitelyTerminates(ifStmt->elseBlock.get());
    }

    if (auto* pick = dynamic_cast<const PickStmt*>(stmt)) {
        if (pick->branches.empty()) {
            return false;
        }

        for (const auto& branch : pick->branches) {
            if (!blockDefinitelyTerminates(branch.body.get())) {
                return false;
            }
        }
        return true;
    }

    if (auto* raw = dynamic_cast<const RawStmt*>(stmt)) {
        return blockDefinitelyTerminates(raw->body.get());
    }

    if (auto* scopeStmt = dynamic_cast<const ScopeStmt*>(stmt)) {
        return blockDefinitelyTerminates(scopeStmt->body.get());
    }

    return false;
}

} // namespace claw::frontend
