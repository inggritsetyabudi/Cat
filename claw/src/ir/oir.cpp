#include "ir/oir.h"

#include "analysis/ownership.h"
#include "analysis/sema.h"
#include "ast/ast.h"

#include <algorithm>
#include <string>
#include <utility>

namespace claw::frontend {

namespace {

std::string quoted(const std::string& text) {
    return '"' + text + '"';
}

std::string binaryOpName(const std::string& op) {
    if (op == "+") return "add";
    if (op == "-") return "sub";
    if (op == "*") return "mul";
    if (op == "/") return "div";
    if (op == "%") return "rem";
    if (op == "==") return "eq";
    if (op == "!=") return "neq";
    if (op == "<") return "lt";
    if (op == "<=") return "lte";
    if (op == ">") return "gt";
    if (op == ">=") return "gte";
    if (op == "..") return "range_exclusive";
    if (op == "..=") return "range_inclusive";
    if (op == "in") return "in";
    return "op";
}

std::vector<OirRealm> canonicalizeRealmOrder(std::vector<OirRealm> realms, std::string_view entryRealm) {
    std::stable_sort(realms.begin(), realms.end(), [&](const OirRealm& left, const OirRealm& right) {
        const bool leftIsEntry = left.name == entryRealm;
        const bool rightIsEntry = right.name == entryRealm;
        if (leftIsEntry != rightIsEntry) {
            return leftIsEntry;
        }
        return left.name < right.name;
    });
    return realms;
}

std::string exprType(const SemanticAnalyzer& sema, const Expr* expr) {
    const auto* type = sema.lookupExprType(expr);
    return type ? type->describe() : std::string("<unknown>");
}

const std::vector<DropAction>* dropsBeforeStmt(const OwnershipResult* ownership, const Stmt* stmt) {
    if (!ownership || !stmt) {
        return nullptr;
    }
    const auto it = ownership->dropsBeforeStmt.find(stmt);
    return it != ownership->dropsBeforeStmt.end() ? &it->second : nullptr;
}

const std::vector<DropAction>* dropsAtBlockEnd(const OwnershipResult* ownership, const BlockStmt* block) {
    if (!ownership || !block) {
        return nullptr;
    }
    const auto it = ownership->dropsAtBlockEnd.find(block);
    return it != ownership->dropsAtBlockEnd.end() ? &it->second : nullptr;
}

std::string calleeText(const Expr* expr) {
    if (!expr) {
        return "<callee>";
    }
    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        return ident->name;
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return calleeText(member->object.get()) + "." + member->member;
    }
    return "<callee>";
}

std::string tailSegment(std::string_view text) {
    const size_t pos = text.rfind('.');
    if (pos == std::string_view::npos) {
        return std::string(text);
    }
    return std::string(text.substr(pos + 1));
}

std::string receiverSegment(std::string_view text) {
    const size_t pos = text.rfind('.');
    if (pos == std::string_view::npos) {
        return {};
    }
    return std::string(text.substr(0, pos));
}

std::optional<OirExternalCallInfo> externalCallInfo(
    const SemanticAnalyzer& sema,
    const CallExpr* call,
    std::string_view callee,
    const std::string& type) {
    if (!call) {
        return std::nullopt;
    }

    if (const auto* signature = sema.lookupCallableSignature(call->callee.get())) {
        if (signature->externalInfo.has_value()) {
            const auto& info = *signature->externalInfo;
            return OirExternalCallInfo{
                info.dependencyRoot,
                info.abi.empty() ? std::string("claw") : info.abi,
                info.linkageName.empty() ? tailSegment(callee) : info.linkageName,
                info.rawOnly,
                false};
        }
    }

    if (type.rfind("opaque external result", 0) != 0) {
        return std::nullopt;
    }

    std::string dependencyRoot = receiverSegment(callee);
    std::string linkageName = tailSegment(callee);
    if (dependencyRoot.empty()) {
        dependencyRoot = linkageName;
    }
    return OirExternalCallInfo{dependencyRoot, "opaque", linkageName, true, true};
}

std::optional<std::string> builtinCallTag(const SemanticAnalyzer& sema, const CallExpr* call) {
    if (!call) {
        return std::nullopt;
    }

    if (const auto methodSignature = sema.lookupMethodSignature(call->callee.get());
        methodSignature.has_value() && methodSignature->isBuiltin) {
        return methodSignature->receiverType.name + "." + methodSignature->name;
    }

    auto* member = dynamic_cast<const MemberExpr*>(call->callee.get());
    if (!member) {
        return std::nullopt;
    }

    auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get());
    if (!objectIdent) {
        return std::nullopt;
    }

    if (objectIdent->name == "Anchor" && member->member == "new") {
        return std::string("Anchor.new");
    }

    if (objectIdent->name == "Arena" && member->member == "new") {
        return std::string("Arena.new");
    }

    return std::nullopt;
}

SymbolLinkInfo localLinkInfo(std::string_view realmName, std::string_view name, bool isShared) {
    SymbolLinkInfo info;
    info.symbol = std::string(realmName.empty() ? std::string_view("<unknown>") : realmName) + "::" + std::string(name);
    info.abi = "claw";
    info.linkage = isShared ? LinkageKind::Shared : LinkageKind::Internal;
    info.ffiStable = false;
    return info;
}

struct LoopTargets {
    std::string continueLabel;
    std::string breakLabel;
};

struct FunctionLoweringContext {
    const SemanticAnalyzer& sema;
    const OwnershipResult* ownership = nullptr;
    const OirEmitter* emitter = nullptr;
    OirFunction function;
    int nextTemp = 0;
    int nextBlock = 0;
    int nextRawRegion = 0;
    std::vector<LoopTargets> loopStack;
    std::vector<std::string> rawRegionStack;

    std::string tempName() {
        return "%t" + std::to_string(nextTemp++);
    }

    std::string blockName(const std::string& prefix) {
        return prefix + "_" + std::to_string(nextBlock++);
    }

    std::string rawRegionName() {
        return "raw_region_" + std::to_string(nextRawRegion++);
    }

    size_t addBlock(const std::string& label) {
        function.blocks.push_back(OirBlock{label, currentRawRegion(), {}});
        return function.blocks.size() - 1;
    }

    const LoopTargets* currentLoop() const {
        return loopStack.empty() ? nullptr : &loopStack.back();
    }

    std::optional<std::string> currentRawRegion() const {
        return rawRegionStack.empty() ? std::nullopt : std::optional<std::string>(rawRegionStack.back());
    }
};

void appendInst(FunctionLoweringContext& context, size_t blockIndex, OirInst inst) {
    context.function.blocks[blockIndex].insts.push_back(std::move(inst));
}

bool hasOwnedTupleField(const ResolvedType& type) {
    if (type.name == "Tuple") {
        return std::any_of(type.params.begin(), type.params.end(), hasOwnedTupleField);
    }
    return type.isOwned();
}

void appendDropForValue(
    FunctionLoweringContext& context,
    size_t blockIndex,
    const std::string& name,
    const ResolvedType& type,
    const std::string& bindingKey = {}) {
    if (type.name == "Tuple") {
        for (size_t i = 0; i < type.params.size(); ++i) {
            const ResolvedType& fieldType = type.params[i];
            if (!hasOwnedTupleField(fieldType)) {
                continue;
            }
            const std::string fieldName = context.tempName();
            appendInst(
                context,
                blockIndex,
                OirFieldInst{
                    fieldName,
                    OirValue{name, type.describe(), false, false, bindingKey},
                    std::to_string(i),
                    fieldType.describe()});
            appendDropForValue(context, blockIndex, fieldName, fieldType);
        }
        return;
    }
    if (type.isOwned()) {
        appendInst(context, blockIndex, OirDropInst{name, type.describe(), bindingKey});
    }
}

void lowerTuplePickPattern(
    const PickPattern* pattern,
    const OirValue& tupleValue,
    const ResolvedType& tupleType,
    FunctionLoweringContext& context,
    size_t blockIndex) {
    if (!pattern || pattern->kind != PickPatternKind::Tuple || tupleType.name != "Tuple") {
        return;
    }

    for (size_t i = 0; i < pattern->elements.size() && i < tupleType.params.size(); ++i) {
        const PickPattern* element = pattern->elements[i].get();
        const ResolvedType& fieldType = tupleType.params[i];
        const std::string type = fieldType.describe();
        if (!element || element->kind == PickPatternKind::Wildcard) {
            continue;
        }

        if (element->kind == PickPatternKind::Tuple) {
            const std::string fieldValueName = context.tempName();
            appendInst(
                context,
                blockIndex,
                OirFieldInst{fieldValueName, tupleValue, std::to_string(i), type});
            lowerTuplePickPattern(
                element,
                OirValue{fieldValueName, type, false},
                fieldType,
                context,
                blockIndex);
            continue;
        }

        if (fieldType.name == "Unit") {
            appendInst(
                context,
                blockIndex,
                OirBindingInst{
                    false,
                    element->name,
                    type,
                    OirValue{"unit", "Unit", true},
                    element->backendBindingKey});
            continue;
        }

        const std::string fieldValueName = context.tempName();
        appendInst(
            context,
            blockIndex,
            OirFieldInst{fieldValueName, tupleValue, std::to_string(i), type});
        appendInst(
                context,
                blockIndex,
                OirBindingInst{
                    false,
                    element->name,
                    type,
                    OirValue{fieldValueName, type, false},
                    element->backendBindingKey});
    }
}

void appendDropInsts(
    FunctionLoweringContext& context,
    size_t blockIndex,
    const std::vector<DropAction>* drops) {
    if (!drops) {
        return;
    }
    for (const auto& drop : *drops) {
        appendDropForValue(context, blockIndex, drop.name, drop.type, drop.bindingKey);
    }
}

OirValue lowerExpr(
    const SemanticAnalyzer& sema,
    const Expr* expr,
    FunctionLoweringContext& context,
    size_t& blockIndex);

std::optional<size_t> lowerStmt(
    const SemanticAnalyzer& sema,
    const OwnershipResult* ownership,
    const Stmt* stmt,
    FunctionLoweringContext& context,
    size_t currentBlockIndex,
    const OirEmitter& emitter);

size_t lowerBlock(
    const SemanticAnalyzer& sema,
    const OwnershipResult* ownership,
    const BlockStmt* block,
    const std::string& label,
    FunctionLoweringContext& context,
    const std::string& fallthroughLabel,
    const OirEmitter& emitter) {
    const size_t entryBlockIndex = context.addBlock(label);
    std::optional<size_t> currentBlockIndex = entryBlockIndex;

    if (!block) {
        if (!fallthroughLabel.empty()) {
            appendInst(context, entryBlockIndex, OirGotoInst{fallthroughLabel});
        }
        return entryBlockIndex;
    }

    for (const auto& stmt : block->statements) {
        if (!currentBlockIndex.has_value()) {
            return entryBlockIndex;
        }
        currentBlockIndex = lowerStmt(
            sema,
            ownership,
            stmt.get(),
            context,
            *currentBlockIndex,
            emitter);
    }

    if (currentBlockIndex.has_value()) {
        appendDropInsts(context, *currentBlockIndex, dropsAtBlockEnd(ownership, block));
        if (!fallthroughLabel.empty()) {
            appendInst(context, *currentBlockIndex, OirGotoInst{fallthroughLabel});
        }
    }

    return entryBlockIndex;
}

bool lowerValueBlock(
    const SemanticAnalyzer& sema,
    const OwnershipResult* ownership,
    const BlockStmt* block,
    const std::string& label,
    const std::string& fallthroughLabel,
    const std::string& resultName,
    bool storeResult,
    FunctionLoweringContext& context,
    const OirEmitter& emitter) {
    std::optional<size_t> currentBlockIndex = context.addBlock(label);
    if (block) {
        for (size_t i = 0; i < block->statements.size() && currentBlockIndex.has_value(); ++i) {
            const Stmt* stmt = block->statements[i].get();
            if (i + 1 == block->statements.size()) {
                if (const auto* tail = dynamic_cast<const ExprStmt*>(stmt); tail && tail->expr) {
                    const OirValue value = lowerExpr(sema, tail->expr.get(), context, *currentBlockIndex);
                    const std::string tailType = exprType(sema, tail->expr.get());
                    if (storeResult && tailType != "Never" && !value.isUnit && !value.terminates) {
                        appendInst(context, *currentBlockIndex, OirStoreInst{resultName, value});
                    }
                    if (tailType == "Never" || value.terminates) {
                        currentBlockIndex.reset();
                    }
                    break;
                }
            }
            currentBlockIndex = lowerStmt(sema, ownership, stmt, context, *currentBlockIndex, emitter);
        }
    }

    if (currentBlockIndex.has_value()) {
        appendDropInsts(context, *currentBlockIndex, dropsAtBlockEnd(ownership, block));
        if (!fallthroughLabel.empty()) {
            appendInst(context, *currentBlockIndex, OirGotoInst{fallthroughLabel});
        }
        return true;
    }
    return false;
}

OirValue lowerExpr(
    const SemanticAnalyzer& sema,
    const Expr* expr,
    FunctionLoweringContext& context,
    size_t& blockIndex) {
    if (!expr) {
        return OirValue{"unit", "Unit", true};
    }

    if (auto* value = dynamic_cast<const IntExpr*>(expr)) {
        return OirValue{value->value, exprType(sema, expr), false};
    }
    if (auto* value = dynamic_cast<const CharExpr*>(expr)) {
        return OirValue{std::to_string(value->value), exprType(sema, expr), false};
    }
    if (auto* value = dynamic_cast<const FloatExpr*>(expr)) {
        return OirValue{value->value, exprType(sema, expr), false};
    }
    if (auto* value = dynamic_cast<const StringExpr*>(expr)) {
        return OirValue{quoted(value->value), exprType(sema, expr), false};
    }
    if (auto* value = dynamic_cast<const BoolExpr*>(expr)) {
        return OirValue{value->value ? "true" : "false", exprType(sema, expr), false};
    }
    if (auto* tuple = dynamic_cast<const TupleExpr*>(expr)) {
        if (tuple->elements.empty()) {
            return OirValue{"unit", "Unit", true};
        }
        const std::string type = exprType(sema, expr);
        const std::string result = context.tempName();
        appendInst(context, blockIndex, OirBindingInst{false, result, type, std::nullopt});
        const OirValue object{result, type, false};
        for (size_t i = 0; i < tuple->elements.size(); ++i) {
            const OirValue element = lowerExpr(sema, tuple->elements[i].get(), context, blockIndex);
            if (element.terminates) {
                return element;
            }
            if (element.isUnit || element.type == "Unit") {
                continue;
            }
            appendInst(context, blockIndex, OirStoreFieldInst{object, std::to_string(i), element});
        }
        return object;
    }
    if (auto* ifExpr = dynamic_cast<const IfExpr*>(expr)) {
        const OirValue condition = lowerExpr(sema, ifExpr->condition.get(), context, blockIndex);
        if (condition.terminates) {
            return condition;
        }
        const std::string type = exprType(sema, expr);
        const bool hasResult = type != "Unit" && type != "Never" && type != "<unknown>";
        const std::string result = hasResult ? context.tempName() : std::string{};
        if (hasResult) {
            appendInst(context, blockIndex, OirBindingInst{true, result, type, std::nullopt});
        }

        const std::string thenLabel = context.blockName("if_then");
        const std::string elseLabel = ifExpr->elseBlock ? context.blockName("if_else") : std::string{};
        const bool thenTerminates = context.emitter->blockDefinitelyTerminates(ifExpr->thenBlock.get());
        const bool elseTerminates = ifExpr->elseBlock && context.emitter->blockDefinitelyTerminates(ifExpr->elseBlock.get());
        const bool needsJoin = !ifExpr->elseBlock || !thenTerminates || !elseTerminates;
        const std::string joinLabel = needsJoin ? context.blockName("if_join") : std::string{};
        appendInst(context, blockIndex, OirBranchInst{
            condition,
            thenLabel,
            ifExpr->elseBlock ? elseLabel : joinLabel});

        const bool thenContinues = lowerValueBlock(
            sema, context.ownership, ifExpr->thenBlock.get(), thenLabel, needsJoin ? joinLabel : std::string{},
            result, hasResult, context, *context.emitter);
        bool elseContinues = !ifExpr->elseBlock;
        if (ifExpr->elseBlock) {
            elseContinues = lowerValueBlock(
                sema, context.ownership, ifExpr->elseBlock.get(), elseLabel, needsJoin ? joinLabel : std::string{},
                result, hasResult, context, *context.emitter);
        }

        if (!thenContinues && !elseContinues) {
            return OirValue{"unit", type, true, true};
        }
        blockIndex = context.addBlock(joinLabel);
        return hasResult ? OirValue{result, type, false} : OirValue{"unit", "Unit", true};
    }
    if (auto* pickExpr = dynamic_cast<const PickExpr*>(expr)) {
        const OirValue value = lowerExpr(sema, pickExpr->value.get(), context, blockIndex);
        if (value.terminates) {
            return value;
        }
        const std::string type = exprType(sema, expr);
        const bool hasResult = type != "Unit" && type != "Never" && type != "<unknown>";
        const std::string result = hasResult ? context.tempName() : std::string{};
        if (hasResult) {
            appendInst(context, blockIndex, OirBindingInst{true, result, type, std::nullopt});
        }

        const auto* pickedType = sema.lookupExprType(pickExpr->value.get());
        if (pickedType && pickedType->name == "Tuple") {
            if (pickExpr->branches.empty()) {
                return OirValue{"unit", type, true, true};
            }
            const auto& branch = pickExpr->branches.front();
            if (branch.tuplePattern) {
                lowerTuplePickPattern(branch.tuplePattern.get(), value, *pickedType, context, blockIndex);
            }

            const bool needsJoin = !context.emitter->blockDefinitelyTerminates(branch.body.get());
            const std::string branchLabel = context.blockName("pick_tuple");
            const std::string joinLabel = needsJoin ? context.blockName("pick_join") : std::string{};
            appendInst(context, blockIndex, OirGotoInst{branchLabel});
            const bool branchContinues = lowerValueBlock(
                sema,
                context.ownership,
                branch.body.get(),
                branchLabel,
                needsJoin ? joinLabel : std::string{},
                result,
                hasResult,
                context,
                *context.emitter);
            if (!branchContinues) {
                return OirValue{"unit", type, true, true};
            }
            blockIndex = context.addBlock(joinLabel);
            return hasResult ? OirValue{result, type, false} : OirValue{"unit", "Unit", true};
        }

        const bool needsJoin = std::any_of(pickExpr->branches.begin(), pickExpr->branches.end(), [&](const PickBranch& branch) {
            return !context.emitter->blockDefinitelyTerminates(branch.body.get());
        });
        const std::string joinLabel = needsJoin ? context.blockName("pick_join") : std::string{};
        std::vector<OirPickCase> cases;
        cases.reserve(pickExpr->branches.size());
        for (const auto& branch : pickExpr->branches) {
            cases.push_back(OirPickCase{branch.tag, branch.bindings, context.blockName("pick_" + branch.tag), branch.bindingKeys});
        }
        appendInst(context, blockIndex, OirPickInst{value, cases});
        bool anyBranchContinues = false;
        for (size_t i = 0; i < pickExpr->branches.size(); ++i) {
            anyBranchContinues = lowerValueBlock(
                sema, context.ownership, pickExpr->branches[i].body.get(), cases[i].targetLabel,
                needsJoin ? joinLabel : std::string{}, result, hasResult, context, *context.emitter) || anyBranchContinues;
        }

        if (!anyBranchContinues) {
            return OirValue{"unit", type, true, true};
        }
        blockIndex = context.addBlock(joinLabel);
        return hasResult ? OirValue{result, type, false} : OirValue{"unit", "Unit", true};
    }
    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        if (const auto ctorIt = sema.result().choiceConstructors.find(ident);
            ctorIt != sema.result().choiceConstructors.end()) {
            const std::string result = context.tempName();
            appendInst(context, blockIndex, OirChoiceMakeInst{
                result,
                ctorIt->second.resultType.describe(),
                ctorIt->second.variantName,
                {}});
            return OirValue{result, exprType(sema, expr), false};
        }
        return OirValue{ident->name, exprType(sema, expr), false, false, ident->backendBindingKey};
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        const OirValue object = lowerExpr(sema, member->object.get(), context, blockIndex);
        if (object.terminates) {
            return object;
        }
        const std::string type = exprType(sema, expr);
        if (type == "Unit") {
            return OirValue{"unit", "Unit", true};
        }
        const std::string result = context.tempName();
        appendInst(context, blockIndex, OirFieldInst{result, object, member->member, type});
        return OirValue{result, type, false};
    }
    if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        const OirValue object = lowerExpr(sema, index->object.get(), context, blockIndex);
        if (object.terminates) {
            return object;
        }
        const OirValue indexValue = lowerExpr(sema, index->index.get(), context, blockIndex);
        if (indexValue.terminates) {
            return indexValue;
        }
        const std::string result = context.tempName();
        const std::string type = exprType(sema, expr);
        appendInst(context, blockIndex, OirIndexInst{result, object, indexValue, type});
        return OirValue{result, type, false};
    }
    if (auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
        OirValue lowered = lowerExpr(sema, borrow->target.get(), context, blockIndex);
        lowered.type = exprType(sema, expr);
        return lowered;
    }
    if (auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        const OirValue operand = lowerExpr(sema, unary->operand.get(), context, blockIndex);
        if (operand.terminates) {
            return operand;
        }
        const std::string result = context.tempName();
        const std::string type = exprType(sema, expr);
        appendInst(context, blockIndex, OirUnaryInst{result, unary->op, operand, type});
        return OirValue{result, type, false};
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
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
        std::string leftTypeName = exprType(sema, binary->left.get());
        std::string fullMethodName = leftTypeName + "." + opMethod;

        if (binary->op == "and" || binary->op == "or") {
            const OirValue left = lowerExpr(sema, binary->left.get(), context, blockIndex);
            if (left.terminates) {
                return left;
            }
            const std::string result = context.tempName();
            appendInst(context, blockIndex, OirBindingInst{true, result, "Bool", std::nullopt});

            const std::string rightLabel = context.blockName("logic_rhs");
            const std::string shortLabel = context.blockName("logic_short");
            const std::string mergeLabel = context.blockName("logic_merge");
            const std::string trueLabel = binary->op == "and" ? rightLabel : shortLabel;
            const std::string falseLabel = binary->op == "and" ? shortLabel : rightLabel;
            appendInst(context, blockIndex, OirBranchInst{left, trueLabel, falseLabel});

            const size_t rightIndex = context.addBlock(rightLabel);
            const size_t shortIndex = context.addBlock(shortLabel);
            const size_t mergeIndex = context.addBlock(mergeLabel);
            appendInst(context, shortIndex, OirStoreInst{result, OirValue{binary->op == "or" ? "true" : "false", "Bool", false}});
            appendInst(context, shortIndex, OirGotoInst{mergeLabel});

            size_t rightBlockIndex = rightIndex;
            const OirValue right = lowerExpr(sema, binary->right.get(), context, rightBlockIndex);
            if (!right.terminates) {
                appendInst(context, rightBlockIndex, OirStoreInst{result, right});
                appendInst(context, rightBlockIndex, OirGotoInst{mergeLabel});
            }
            blockIndex = mergeIndex;
            return OirValue{result, "Bool", false};
        }

        if (!opMethod.empty() && sema.lookupFunctionSignature(fullMethodName)) {
            const OirValue left = lowerExpr(sema, binary->left.get(), context, blockIndex);
            if (left.terminates) {
                return left;
            }
            const OirValue right = lowerExpr(sema, binary->right.get(), context, blockIndex);
            if (right.terminates) {
                return right;
            }
            const std::string result = context.tempName();
            const std::string type = exprType(sema, expr);
            std::vector<OirValue> args{left, right};
            appendInst(context, blockIndex, OirCallInst{result, fullMethodName, std::move(args), type});
            return OirValue{result, type, false};
        }

        const OirValue left = lowerExpr(sema, binary->left.get(), context, blockIndex);
        if (left.terminates) {
            return left;
        }
        const OirValue right = lowerExpr(sema, binary->right.get(), context, blockIndex);
        if (right.terminates) {
            return right;
        }
        const std::string result = context.tempName();
        const std::string type = exprType(sema, expr);
        appendInst(context, blockIndex, OirBinaryInst{result, binaryOpName(binary->op), left, right, type});
        return OirValue{result, type, false};
    }
    if (auto* shapeInit = dynamic_cast<const ShapeInitExpr*>(expr)) {
        const std::string type = exprType(sema, expr);
        const std::string result = context.tempName();
        appendInst(context, blockIndex, OirBindingInst{false, result, type, std::nullopt});
        const OirValue object{result, type, false};
        for (const auto& field : shapeInit->fields) {
            const OirValue fieldValue = lowerExpr(sema, field.value.get(), context, blockIndex);
            if (fieldValue.terminates) {
                return fieldValue;
            }
            appendInst(context, blockIndex, OirStoreFieldInst{object, field.name, fieldValue});
        }
        return object;
    }
    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        if (const auto ctorIt = sema.result().choiceConstructors.find(call);
            ctorIt != sema.result().choiceConstructors.end()) {
            std::vector<OirValue> payloads;
            payloads.reserve(call->args.size());
            for (const auto& arg : call->args) {
                OirValue payload = lowerExpr(sema, arg.get(), context, blockIndex);
                if (payload.terminates) {
                    return payload;
                }
                payloads.push_back(std::move(payload));
            }

            const std::string result = context.tempName();
            appendInst(context, blockIndex, OirChoiceMakeInst{
                result,
                ctorIt->second.resultType.describe(),
                ctorIt->second.variantName,
                std::move(payloads)});
            return OirValue{result, exprType(sema, expr), false};
        }

        const auto callBuiltinTag = builtinCallTag(sema, call);
        std::string callee;
        std::vector<OirValue> args;
        std::optional<OirValue> builtinReceiver;
        if (auto* memberCallee = dynamic_cast<const MemberExpr*>(call->callee.get())) {
            const auto methodSig = sema.lookupMethodSignature(call->callee.get());
            if (methodSig.has_value()) {
                const OirValue receiver = lowerExpr(sema, memberCallee->object.get(), context, blockIndex);
                if (receiver.terminates) {
                    return receiver;
                }
                if (callBuiltinTag.has_value()) {
                    callee = receiver.text + "." + memberCallee->member;
                    builtinReceiver = receiver;
                } else {
                    callee = methodSig->receiverType.name + "." + memberCallee->member;
                    args.push_back(receiver);
                }
            } else {
                callee = calleeText(call->callee.get());
            }
        } else {
            callee = calleeText(call->callee.get());
        }

        args.reserve(args.size() + call->args.size());
        for (const auto& arg : call->args) {
            OirValue loweredArg = lowerExpr(sema, arg.get(), context, blockIndex);
            if (loweredArg.terminates) {
                return loweredArg;
            }
            args.push_back(std::move(loweredArg));
        }

        const std::string type = exprType(sema, expr);
        const auto callExternalInfo = externalCallInfo(sema, call, callee, type);
        if (type == "Unit") {
            appendInst(context, blockIndex, OirCallInst{
                std::nullopt, callee, std::move(args), type, callBuiltinTag, callExternalInfo, builtinReceiver});
            return OirValue{"unit", "Unit", true};
        }

        const std::string result = context.tempName();
        appendInst(context, blockIndex, OirCallInst{
            result, callee, std::move(args), type, callBuiltinTag, callExternalInfo, builtinReceiver});
        return OirValue{result, type, false};
    }

    return OirValue{"<expr>", exprType(sema, expr), false};
}

std::optional<size_t> lowerStmt(
    const SemanticAnalyzer& sema,
    const OwnershipResult* ownership,
    const Stmt* stmt,
    FunctionLoweringContext& context,
    size_t currentBlockIndex,
    const OirEmitter& emitter) {
    if (auto* bind = dynamic_cast<const BindingStmt*>(stmt)) {
        if (!bind->tuplePatternNames.empty()) {
            if (!bind->value) {
                return currentBlockIndex;
            }
            const auto* tupleType = sema.lookupExprType(bind->value.get());
            OirValue tupleValue = lowerExpr(sema, bind->value.get(), context, currentBlockIndex);
            if (tupleValue.terminates) {
                return std::nullopt;
            }
            for (size_t i = 0; i < bind->tuplePatternNames.size(); ++i) {
                const std::string& name = bind->tuplePatternNames[i];
                const ResolvedType fieldType = tupleType && i < tupleType->params.size()
                    ? tupleType->params[i]
                    : makeUnknownType();
                if (name == "_") {
                    if (hasOwnedTupleField(fieldType)) {
                        const std::string fieldValueName = context.tempName();
                        appendInst(
                            context,
                            currentBlockIndex,
                            OirFieldInst{fieldValueName, tupleValue, std::to_string(i), fieldType.describe()});
                        appendDropForValue(context, currentBlockIndex, fieldValueName, fieldType);
                    }
                    continue;
                }
                const std::string type = fieldType.describe();
                if (fieldType.name == "Unit") {
                appendInst(
                    context,
                    currentBlockIndex,
                    OirBindingInst{
                        false,
                        name,
                        type,
                        OirValue{"unit", "Unit", true},
                        i < bind->tuplePatternBindingKeys.size() ? bind->tuplePatternBindingKeys[i] : std::string{}});
                    continue;
                }
                const std::string fieldValueName = context.tempName();
                appendInst(
                    context,
                    currentBlockIndex,
                    OirFieldInst{fieldValueName, tupleValue, std::to_string(i), type});
                appendInst(
                    context,
                    currentBlockIndex,
                    OirBindingInst{
                        false,
                        name,
                        type,
                        OirValue{fieldValueName, type, false},
                        i < bind->tuplePatternBindingKeys.size() ? bind->tuplePatternBindingKeys[i] : std::string{}});
            }
            return currentBlockIndex;
        }

        const auto bindingIt = sema.result().bindingTypes.find(bind);
        const std::string type = bindingIt != sema.result().bindingTypes.end()
            ? bindingIt->second.describe()
            : std::string("<unknown>");
        std::optional<OirValue> init;
        if (bind->value) {
            init = lowerExpr(sema, bind->value.get(), context, currentBlockIndex);
            if (init->terminates) {
                return std::nullopt;
            }
        }
        appendInst(context, currentBlockIndex, OirBindingInst{
            bind->isMutable, bind->name, type, init, bind->backendBindingKey});
        return currentBlockIndex;
    }

    if (auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        OirValue value;
        std::optional<OirValue> compoundMemberObject;
        std::optional<OirValue> indexedObject;
        std::optional<OirValue> indexedIndex;
        if (assign->op == "=") {
            if (auto* index = dynamic_cast<const IndexExpr*>(assign->target.get())) {
                indexedObject = lowerExpr(sema, index->object.get(), context, currentBlockIndex);
                if (indexedObject->terminates) {
                    return std::nullopt;
                }
                indexedIndex = lowerExpr(sema, index->index.get(), context, currentBlockIndex);
                if (indexedIndex->terminates) {
                    return std::nullopt;
                }
            }
            value = lowerExpr(sema, assign->value.get(), context, currentBlockIndex);
            if (value.terminates) {
                return std::nullopt;
            }
        } else {
            const std::string binaryOperator = assign->op.substr(0, assign->op.size() - 1);
            OirValue oldValue;
            if (auto* ident = dynamic_cast<const IdentExpr*>(assign->target.get())) {
                oldValue = OirValue{
                    ident->name,
                    exprType(sema, assign->target.get()),
                    false,
                    false,
                    ident->backendBindingKey};
            } else if (auto* member = dynamic_cast<const MemberExpr*>(assign->target.get())) {
                compoundMemberObject = lowerExpr(sema, member->object.get(), context, currentBlockIndex);
                if (compoundMemberObject->terminates) {
                    return std::nullopt;
                }
                const std::string oldResult = context.tempName();
                appendInst(context, currentBlockIndex, OirFieldInst{
                    oldResult, *compoundMemberObject, member->member, exprType(sema, assign->target.get())});
                oldValue = OirValue{oldResult, exprType(sema, assign->target.get()), false};
            } else if (auto* index = dynamic_cast<const IndexExpr*>(assign->target.get())) {
                indexedObject = lowerExpr(sema, index->object.get(), context, currentBlockIndex);
                if (indexedObject->terminates) {
                    return std::nullopt;
                }
                indexedIndex = lowerExpr(sema, index->index.get(), context, currentBlockIndex);
                if (indexedIndex->terminates) {
                    return std::nullopt;
                }
                const std::string oldResult = context.tempName();
                appendInst(context, currentBlockIndex, OirIndexInst{
                    oldResult, *indexedObject, *indexedIndex, exprType(sema, assign->target.get())});
                oldValue = OirValue{oldResult, exprType(sema, assign->target.get()), false};
            } else {
                oldValue = OirValue{"<unsupported>", exprType(sema, assign->target.get()), false};
            }

            const OirValue right = lowerExpr(sema, assign->value.get(), context, currentBlockIndex);
            if (right.terminates) {
                return std::nullopt;
            }
            const std::string result = context.tempName();
            const std::string type = exprType(sema, assign->target.get());
            appendInst(context, currentBlockIndex, OirBinaryInst{result, binaryOpName(binaryOperator), oldValue, right, type});
            value = OirValue{result, type, false};
        }
        appendDropInsts(context, currentBlockIndex, dropsBeforeStmt(ownership, stmt));
        if (auto* ident = dynamic_cast<const IdentExpr*>(assign->target.get())) {
            appendInst(context, currentBlockIndex, OirStoreInst{ident->name, value, ident->backendBindingKey});
            return currentBlockIndex;
        }
        if (auto* member = dynamic_cast<const MemberExpr*>(assign->target.get())) {
            const OirValue object = compoundMemberObject.has_value()
                ? *compoundMemberObject
                : lowerExpr(sema, member->object.get(), context, currentBlockIndex);
            appendInst(context, currentBlockIndex, OirStoreFieldInst{object, member->member, value});
            return currentBlockIndex;
        }
        if (dynamic_cast<const IndexExpr*>(assign->target.get())) {
            appendInst(context, currentBlockIndex, OirStoreIndexInst{*indexedObject, *indexedIndex, value});
            return currentBlockIndex;
        }
        appendInst(context, currentBlockIndex, OirStoreInst{"<unsupported>", value});
        return currentBlockIndex;
    }

    if (auto* returnStmt = dynamic_cast<const ReturnStmt*>(stmt)) {
        const OirValue value = returnStmt->value
            ? lowerExpr(sema, returnStmt->value.get(), context, currentBlockIndex)
            : OirValue{"unit", "Unit", true};
        if (value.terminates) {
            return std::nullopt;
        }
        const auto* returnType = returnStmt->value ? sema.lookupExprType(returnStmt->value.get()) : nullptr;
        if (returnType && returnType->name == "Never") {
            return std::nullopt;
        }
        appendDropInsts(context, currentBlockIndex, dropsBeforeStmt(ownership, stmt));
        appendInst(context, currentBlockIndex, OirReturnInst{value});
        return std::nullopt;
    }

    if (auto* exprStmt = dynamic_cast<const ExprStmt*>(stmt)) {
        const OirValue value = lowerExpr(sema, exprStmt->expr.get(), context, currentBlockIndex);
        if (value.terminates) {
            return std::nullopt;
        }
        const auto* exprType = exprStmt->expr ? sema.lookupExprType(exprStmt->expr.get()) : nullptr;
        if (exprType && exprType->name == "Never") {
            return std::nullopt;
        }
        if (!value.isUnit) {
            appendInst(context, currentBlockIndex, OirDiscardInst{value});
            if (exprType && exprType->isOwned() && exprType->name == "Anchor" &&
                !sema.isPlaceExpression(exprStmt->expr.get())) {
                appendInst(context, currentBlockIndex, OirDropInst{value.text, exprType->describe()});
            }
        }
        return currentBlockIndex;
    }

    if (auto* ifStmt = dynamic_cast<const IfStmt*>(stmt)) {
        const OirValue condition = lowerExpr(sema, ifStmt->condition.get(), context, currentBlockIndex);
        if (condition.terminates) {
            return std::nullopt;
        }
        const std::string thenLabel = context.blockName("if_then");
        if (ifStmt->elseBlock) {
            const std::string elseLabel = context.blockName("if_else");
            const bool thenTerminates = emitter.blockDefinitelyTerminates(ifStmt->thenBlock.get());
            const bool elseTerminates = emitter.blockDefinitelyTerminates(ifStmt->elseBlock.get());
            const bool needsJoin = !thenTerminates || !elseTerminates;
            const std::string joinLabel = needsJoin ? context.blockName("if_join") : std::string{};
            appendInst(context, currentBlockIndex, OirBranchInst{condition, thenLabel, elseLabel});
            lowerBlock(sema, ownership, ifStmt->thenBlock.get(), thenLabel, context, needsJoin ? joinLabel : std::string{}, emitter);
            lowerBlock(sema, ownership, ifStmt->elseBlock.get(), elseLabel, context, needsJoin ? joinLabel : std::string{}, emitter);
            if (!needsJoin) {
                return std::nullopt;
            }
            return context.addBlock(joinLabel);
        }

        const std::string joinLabel = context.blockName("if_join");
        appendInst(context, currentBlockIndex, OirBranchInst{condition, thenLabel, joinLabel});
        lowerBlock(sema, ownership, ifStmt->thenBlock.get(), thenLabel, context, joinLabel, emitter);
        return context.addBlock(joinLabel);
    }

    if (auto* loop = dynamic_cast<const LoopStmt*>(stmt)) {
        const std::string headerLabel = context.blockName("loop_header");
        const std::string bodyLabel = context.blockName("loop_body");
        const std::string exitLabel = context.blockName("loop_exit");
        appendInst(context, currentBlockIndex, OirGotoInst{headerLabel});
        size_t headerIndex = context.addBlock(headerLabel);
        if (loop->condition) {
            const OirValue condition = lowerExpr(sema, loop->condition.get(), context, headerIndex);
            if (condition.terminates) {
                return std::nullopt;
            }
            appendInst(context, headerIndex, OirBranchInst{condition, bodyLabel, exitLabel});
        } else {
            appendInst(context, headerIndex, OirGotoInst{bodyLabel});
        }
        context.loopStack.push_back(LoopTargets{headerLabel, exitLabel});
        lowerBlock(sema, ownership, loop->body.get(), bodyLabel, context, headerLabel, emitter);
        context.loopStack.pop_back();
        return context.addBlock(exitLabel);
    }

    if (auto* scan = dynamic_cast<const ScanStmt*>(stmt)) {
        const OirValue iterable = lowerExpr(sema, scan->iterable.get(), context, currentBlockIndex);
        if (iterable.terminates) {
            return std::nullopt;
        }
        const auto itemTypeIt = sema.result().scanItemTypes.find(scan);
        const std::string itemType = itemTypeIt != sema.result().scanItemTypes.end()
            ? itemTypeIt->second.describe()
            : std::string("<unknown>");
        const std::string headerLabel = context.blockName("scan_header");
        const std::string bodyLabel = context.blockName("scan_body");
        const std::string exitLabel = context.blockName("scan_exit");
        if (iterable.type == "Range[Int32]") {
            appendInst(
                context,
                currentBlockIndex,
                OirRangeScanInitInst{
                    headerLabel + "::" + scan->itemName,
                    scan->itemName,
                    scan->indexName,
                    itemType,
                    iterable,
                    scan->itemBindingKey,
                    scan->indexBindingKey});
        }
        appendInst(context, currentBlockIndex, OirGotoInst{headerLabel});
        const size_t headerIndex = context.addBlock(headerLabel);
        appendInst(
            context,
            headerIndex,
            OirScanInst{
                scan->itemName,
                scan->indexName,
                itemType,
                iterable,
                bodyLabel,
                exitLabel,
                scan->itemBindingKey,
                scan->indexBindingKey});
        context.loopStack.push_back(LoopTargets{headerLabel, exitLabel});
        lowerBlock(sema, ownership, scan->body.get(), bodyLabel, context, headerLabel, emitter);
        context.loopStack.pop_back();
        return context.addBlock(exitLabel);
    }

    if (auto* scope = dynamic_cast<const ScopeStmt*>(stmt)) {
        const std::string scopeLabel = context.blockName("scope_" + scope->name);
        const std::string contLabel = context.blockName("scope_cont");
        appendInst(context, currentBlockIndex, OirGotoInst{scopeLabel});
        lowerBlock(sema, ownership, scope->body.get(), scopeLabel, context, contLabel, emitter);
        return context.addBlock(contLabel);
    }

    if (auto* pick = dynamic_cast<const PickStmt*>(stmt)) {
        const OirValue value = lowerExpr(sema, pick->value.get(), context, currentBlockIndex);
        if (value.terminates) {
            return std::nullopt;
        }
        const auto* pickedType = sema.lookupExprType(pick->value.get());
        if (pickedType && pickedType->name == "Tuple") {
            if (pick->branches.empty()) {
                return std::nullopt;
            }
            const auto& branch = pick->branches.front();
            if (branch.tuplePattern) {
                lowerTuplePickPattern(branch.tuplePattern.get(), value, *pickedType, context, currentBlockIndex);
            }

            const bool needsJoin = !emitter.blockDefinitelyTerminates(branch.body.get());
            const std::string branchLabel = context.blockName("pick_tuple");
            const std::string joinLabel = needsJoin ? context.blockName("pick_join") : std::string{};
            appendInst(context, currentBlockIndex, OirGotoInst{branchLabel});
            lowerBlock(
                sema,
                ownership,
                branch.body.get(),
                branchLabel,
                context,
                needsJoin ? joinLabel : std::string{},
                emitter);
            if (!needsJoin) {
                return std::nullopt;
            }
            return context.addBlock(joinLabel);
        }

        const bool needsJoin = [&]() {
            for (const auto& branch : pick->branches) {
                if (!emitter.blockDefinitelyTerminates(branch.body.get())) {
                    return true;
                }
            }
            return false;
        }();
        const std::string joinLabel = needsJoin ? context.blockName("pick_join") : std::string{};
        std::vector<OirPickCase> cases;
        cases.reserve(pick->branches.size());
        for (const auto& branch : pick->branches) {
            cases.push_back(OirPickCase{branch.tag, branch.bindings, context.blockName("pick_" + branch.tag), branch.bindingKeys});
        }
        appendInst(context, currentBlockIndex, OirPickInst{value, cases});
        for (size_t i = 0; i < pick->branches.size(); ++i) {
            lowerBlock(
                sema,
                ownership,
                pick->branches[i].body.get(),
                cases[i].targetLabel,
                context,
                needsJoin ? joinLabel : std::string{},
                emitter);
        }
        if (!needsJoin) {
            return std::nullopt;
        }
        return context.addBlock(joinLabel);
    }

    if (auto* tryStmt = dynamic_cast<const TryStmt*>(stmt)) {
        const OirValue value = lowerExpr(sema, tryStmt->expr.get(), context, currentBlockIndex);
        if (value.terminates) {
            return std::nullopt;
        }
        if (tryStmt->autoPropagate) {
            appendInst(context, currentBlockIndex, OirTryInst{value, tryStmt->name, {}, {}, true, tryStmt->backendBindingKey, {}});
            return currentBlockIndex;
        }

        const std::string failLabel = context.blockName("try_fail");
        appendInst(context, currentBlockIndex, OirTryInst{
            value, tryStmt->name, tryStmt->failName, failLabel, false, tryStmt->backendBindingKey, tryStmt->failBindingKey});
        lowerBlock(sema, ownership, tryStmt->failBlock.get(), failLabel, context, {}, emitter);
        return currentBlockIndex;
    }

    if (auto* raw = dynamic_cast<const RawStmt*>(stmt)) {
        const std::string rawLabel = context.blockName("raw");
        const std::string contLabel = context.blockName("raw_cont");
        const std::string rawRegion = context.rawRegionName();
        appendInst(context, currentBlockIndex, OirGotoInst{rawLabel});
        context.rawRegionStack.push_back(rawRegion);
        lowerBlock(sema, ownership, raw->body.get(), rawLabel, context, contLabel, emitter);
        context.rawRegionStack.pop_back();
        return context.addBlock(contLabel);
    }

    if (dynamic_cast<const StopStmt*>(stmt)) {
        appendDropInsts(context, currentBlockIndex, dropsBeforeStmt(ownership, stmt));
        const auto* loop = context.currentLoop();
        appendInst(context, currentBlockIndex, OirStopInst{loop ? loop->breakLabel : std::string{}});
        return std::nullopt;
    }

    if (dynamic_cast<const SkipStmt*>(stmt)) {
        appendDropInsts(context, currentBlockIndex, dropsBeforeStmt(ownership, stmt));
        const auto* loop = context.currentLoop();
        appendInst(context, currentBlockIndex, OirSkipInst{loop ? loop->continueLabel : std::string{}});
        return std::nullopt;
    }

    return currentBlockIndex;
}

bool blockDefinitelyTerminatesImpl(const OirEmitter& emitter, const BlockStmt* block) {
    if (!block) {
        return false;
    }

    for (const auto& stmt : block->statements) {
        if (emitter.stmtDefinitelyTerminates(stmt.get())) {
            return true;
        }
    }
    return false;
}

bool stmtDefinitelyTerminatesImpl(const OirEmitter& emitter, const Stmt* stmt) {
    if (dynamic_cast<const ReturnStmt*>(stmt) || dynamic_cast<const StopStmt*>(stmt) || dynamic_cast<const SkipStmt*>(stmt)) {
        return true;
    }

    if (const auto* binding = dynamic_cast<const BindingStmt*>(stmt)) {
        return binding->value && emitter.exprDefinitelyTerminates(binding->value.get());
    }

    if (const auto* assignment = dynamic_cast<const AssignStmt*>(stmt)) {
        return assignment->value && emitter.exprDefinitelyTerminates(assignment->value.get());
    }

    if (auto* exprStmt = dynamic_cast<const ExprStmt*>(stmt)) {
        if (auto* ifExpr = dynamic_cast<const IfExpr*>(exprStmt->expr.get())) {
            return ifExpr->elseBlock && emitter.blockDefinitelyTerminates(ifExpr->thenBlock.get()) &&
                   emitter.blockDefinitelyTerminates(ifExpr->elseBlock.get());
        }
        if (auto* pickExpr = dynamic_cast<const PickExpr*>(exprStmt->expr.get())) {
            return !pickExpr->branches.empty() && std::all_of(
                pickExpr->branches.begin(),
                pickExpr->branches.end(),
                [&](const PickBranch& branch) { return emitter.blockDefinitelyTerminates(branch.body.get()); });
        }
        return emitter.exprDefinitelyTerminates(exprStmt->expr.get());
    }

    if (auto* ifStmt = dynamic_cast<const IfStmt*>(stmt)) {
        return ifStmt->elseBlock && emitter.blockDefinitelyTerminates(ifStmt->thenBlock.get()) &&
               emitter.blockDefinitelyTerminates(ifStmt->elseBlock.get());
    }

    if (auto* pick = dynamic_cast<const PickStmt*>(stmt)) {
        if (pick->branches.empty()) {
            return false;
        }
        for (const auto& branch : pick->branches) {
            if (!emitter.blockDefinitelyTerminates(branch.body.get())) {
                return false;
            }
        }
        return true;
    }

    if (auto* raw = dynamic_cast<const RawStmt*>(stmt)) {
        return emitter.blockDefinitelyTerminates(raw->body.get());
    }

    if (auto* scopeStmt = dynamic_cast<const ScopeStmt*>(stmt)) {
        return emitter.blockDefinitelyTerminates(scopeStmt->body.get());
    }

    return false;
}

} // namespace

OirEmitter::OirEmitter(const SemanticAnalyzer& sema, const OwnershipResult* ownership)
    : sema(sema), ownership(ownership) {}

OirRealm OirEmitter::lowerRealm(const RealmDecl* realm) const {
    OirRealm lowered;
    lowered.name = realm ? realm->name : std::string("<unknown>");
    if (!realm) {
        return lowered;
    }

    for (const auto& decl : realm->declarations) {
        if (auto loweredDecl = lowerDecl(decl.get(), lowered.name)) {
            lowered.decls.push_back(std::move(*loweredDecl));
        }
        if (auto* impl = dynamic_cast<const ImplementsDecl*>(decl.get())) {
            for (const auto& method : impl->methods) {
                std::string fullMethodName = impl->typeName + "." + method->name;
                lowered.decls.push_back(OirDecl{lowerFn(method.get(), lowered.name, fullMethodName)});
            }
        }
    }
    return lowered;
}

std::string OirEmitter::emit(const RealmDecl* realm) const {
    return formatOirRealm(lowerRealm(realm));
}

std::optional<OirDecl> OirEmitter::lowerDecl(const Decl* decl, std::string_view realmName) const {
    if (auto* fn = dynamic_cast<const FnDecl*>(decl)) {
        return OirDecl{lowerFn(fn, realmName)};
    }
    if (auto* shape = dynamic_cast<const ShapeDecl*>(decl)) {
        return OirDecl{lowerShape(shape, realmName)};
    }
    if (auto* choice = dynamic_cast<const ChoiceDecl*>(decl)) {
        return OirDecl{lowerChoice(choice, realmName)};
    }
    if (auto* stat = dynamic_cast<const StaticDecl*>(decl)) {
        return OirDecl{lowerStatic(stat, realmName)};
    }
    return std::nullopt;
}

OirFunction OirEmitter::lowerFn(const FnDecl* fn, std::string_view realmName, std::string_view overrideName) const {
    FunctionLoweringContext context{sema, ownership};
    context.emitter = this;
    context.function.name = !overrideName.empty() ? std::string(overrideName) : (fn ? fn->name : std::string("<unknown>"));
    context.function.linkage = localLinkInfo(realmName, context.function.name, fn && fn->isShared);

    const auto* signature = !overrideName.empty() ? sema.lookupFunctionSignature(std::string(overrideName)) : sema.lookupFunctionSignature(fn);
    if (fn) {
        size_t nonSelfParamIdx = 0;
        for (size_t i = 0; i < fn->params.size(); ++i) {
            ResolvedType paramType;
            if (fn->params[i].isSelf) {
                paramType = signature && signature->receiverType.has_value() 
                    ? signature->receiverType.value() 
                    : makeUnknownType();
            } else {
                paramType = signature && nonSelfParamIdx < signature->paramTypes.size()
                    ? signature->paramTypes[nonSelfParamIdx++]
                    : makeUnknownType();
            }
            const auto layout = computeTypeLayout(paramType, sema.result(), sema.targetSpec());
            context.function.params.push_back(OirParam{
                fn->params[i].name,
                paramType.describe(),
                layout.has_value() ? layout->passKind : AbiPassKind::Unknown});
        }
    }
    const ResolvedType returnType = signature ? signature->returnType : makeUnknownType();
    context.function.returnType = returnType.describe();
    const auto returnLayout = computeTypeLayout(returnType, sema.result(), sema.targetSpec());
    context.function.returnPassKind = returnLayout.has_value() ? returnLayout->passKind : AbiPassKind::Unknown;

    const size_t entryBlockIndex = context.addBlock("entry");
    std::optional<size_t> currentBlockIndex = entryBlockIndex;

    if (fn && fn->body) {
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
            if (!currentBlockIndex.has_value()) {
                break;
            }

            if (implicitTailExpr && stmt.get() == implicitTailExpr) {
                const OirValue value = implicitTailExpr->expr
                    ? lowerExpr(sema, implicitTailExpr->expr.get(), context, *currentBlockIndex)
                    : OirValue{"unit", "Unit", true};
                const auto* valueType = implicitTailExpr->expr ? sema.lookupExprType(implicitTailExpr->expr.get()) : nullptr;
                if (valueType && valueType->name == "Never") {
                    currentBlockIndex = std::nullopt;
                    break;
                }
                appendDropInsts(context, *currentBlockIndex, dropsAtBlockEnd(ownership, fn->body.get()));
                appendInst(context, *currentBlockIndex, OirReturnInst{value});
                currentBlockIndex = std::nullopt;
                break;
            }

            currentBlockIndex = lowerStmt(sema, ownership, stmt.get(), context, *currentBlockIndex, *this);
        }

        if (currentBlockIndex.has_value()) {
            appendDropInsts(context, *currentBlockIndex, dropsAtBlockEnd(ownership, fn->body.get()));
        }
    }

    return std::move(context.function);
}

OirShape OirEmitter::lowerShape(const ShapeDecl* shape, std::string_view realmName) const {
    OirShape lowered;
    lowered.name = shape ? shape->name : std::string("<unknown>");
    lowered.linkage = localLinkInfo(realmName, lowered.name, shape && shape->isShared);
    if (!shape) {
        return lowered;
    }

    const auto* info = sema.lookupShape(shape->name);
    if (!info) {
        return lowered;
    }

    lowered.isViewShape = info->isViewShape;
    lowered.scopeParamName = info->scopeParamName;
    lowered.typeParams = info->typeParams;
    ResolvedType namedType = makeUnknownType();
    namedType.name = shape->name;
    namedType.category = TypeCategory::Owned;
    lowered.layout = computeTypeLayout(namedType, sema.result(), sema.targetSpec());

    for (const auto& fieldName : info->fieldOrder) {
        const auto it = info->fields.find(fieldName);
        if (it == info->fields.end()) {
            continue;
        }
        lowered.fields.push_back(OirShapeField{fieldName, it->second.describe()});
    }
    return lowered;
}

OirChoice OirEmitter::lowerChoice(const ChoiceDecl* choice, std::string_view realmName) const {
    OirChoice lowered;
    lowered.name = choice ? choice->name : std::string("<unknown>");
    lowered.linkage = localLinkInfo(realmName, lowered.name, choice && choice->isShared);
    if (!choice) {
        return lowered;
    }

    const auto* info = sema.lookupChoice(choice->name);
    if (!info) {
        return lowered;
    }

    lowered.typeParams = info->typeParams;
    ResolvedType namedType = makeUnknownType();
    namedType.name = choice->name;
    namedType.category = TypeCategory::Owned;
    lowered.layout = computeTypeLayout(namedType, sema.result(), sema.targetSpec());

    for (const auto& variantName : info->variantOrder) {
        OirChoiceCase item;
        item.name = variantName;
        const auto it = info->variants.find(variantName);
        if (it != info->variants.end()) {
            for (const auto& payloadType : it->second.payloadTypes) {
                item.payloadTypes.push_back(payloadType.describe());
            }
        }
        lowered.cases.push_back(std::move(item));
    }
    return lowered;
}

OirStatic OirEmitter::lowerStatic(const StaticDecl* stat, std::string_view realmName) const {
    OirStatic lowered;
    lowered.name = stat ? stat->name : std::string("<unknown>");
    lowered.link = localLinkInfo(realmName, lowered.name, false);
    
    if (stat && stat->init) {
        lowered.type = exprType(sema, stat->init.get());
        if (auto* intLit = dynamic_cast<IntExpr*>(stat->init.get())) {
            lowered.value = intLit->value;
        } else if (auto* charLit = dynamic_cast<CharExpr*>(stat->init.get())) {
            lowered.value = std::to_string(charLit->value);
        } else if (auto* floatLit = dynamic_cast<FloatExpr*>(stat->init.get())) {
            lowered.value = floatLit->value;
        } else if (auto* strLit = dynamic_cast<StringExpr*>(stat->init.get())) {
            lowered.value = strLit->value;
        } else if (auto* boolLit = dynamic_cast<BoolExpr*>(stat->init.get())) {
            lowered.value = boolLit->value ? "true" : "false";
        } else {
            lowered.value = "<unknown_init>";
        }
    } else {
        lowered.type = "<unknown>";
        lowered.value = "<unknown_init>";
    }
    
    return lowered;
}

bool OirEmitter::blockDefinitelyTerminates(const BlockStmt* block) const {
    return blockDefinitelyTerminatesImpl(*this, block);
}

bool OirEmitter::stmtDefinitelyTerminates(const Stmt* stmt) const {
    return stmtDefinitelyTerminatesImpl(*this, stmt);
}

bool OirEmitter::exprDefinitelyTerminates(const Expr* expr) const {
    const auto* type = sema.lookupExprType(expr);
    return type && type->name == "Never";
}

OirProgram buildOirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    OirProgram program;
    program.entryRealm = entryRealm.empty() ? std::string("<unknown>") : std::string(entryRealm);
    program.entrySymbol = program.entryRealm + "::main";

    for (const auto& unit : units) {
        if (!unit.realm || !unit.sema) {
            program.realms.push_back(OirRealm{"<unknown>", {}});
            continue;
        }
        OirEmitter emitter(*unit.sema, unit.ownership);
        program.realms.push_back(emitter.lowerRealm(unit.realm));
    }

    program.realms = canonicalizeRealmOrder(std::move(program.realms), program.entryRealm);
    return program;
}

} // namespace claw::frontend
