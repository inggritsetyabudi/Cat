#include "codegen/llvm_internal.h"

namespace claw::codegen {

void LlvmEmitter::emitRangeScanInit(
    const LirRangeScanInitInst& value,
    FunctionState& state,
    std::vector<std::string>& lines) {
    IteratorState& iterState = ensureRangeIteratorState(
        value.stateKey,
        value.itemName,
        value.indexName,
        value.itemType,
        state);
    const std::string rangeValue = ensureValue(value.iterable, state, lines);
    const std::string rangeType = llvmType(value.iterable.type);
    const std::string start = state.temp("range.scan.start");
    const std::string end = state.temp("range.scan.end");
    const std::string inclusive = state.temp("range.scan.inclusive");
    lines.push_back("  " + start + " = extractvalue " + rangeType + " " + rangeValue + ", 0");
    lines.push_back("  " + end + " = extractvalue " + rangeType + " " + rangeValue + ", 1");
    lines.push_back("  " + inclusive + " = extractvalue " + rangeType + " " + rangeValue + ", 2");
    lines.push_back("  store i32 " + start + ", ptr " + iterState.rangeCurrentSlot + ", align 4");
    lines.push_back("  store i32 " + end + ", ptr " + iterState.rangeEndSlot + ", align 4");
    lines.push_back("  store i1 " + inclusive + ", ptr " + iterState.rangeInclusiveSlot + ", align 1");
    lines.push_back("  store i64 0, ptr " + iterState.indexSlot + ", align 8");
    lines.push_back("  store i1 false, ptr " + iterState.rangeFinishedSlot + ", align 1");
}

void LlvmEmitter::emitIterNext(
    const LirIterNextInst& value,
    std::string_view currentBlockLabel,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const std::string iterableBase = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(value.iterable.type)));
    if (iterableBase == "Range") {
        const std::string stateKey = std::string(currentBlockLabel) + "::" + value.itemName;
        IteratorState& iterState = ensureRangeIteratorState(
            stateKey,
            value.itemName,
            value.indexName,
            value.itemType,
            state);
        const std::string current = state.temp("range.scan.current");
        const std::string end = state.temp("range.scan.end");
        const std::string inclusive = state.temp("range.scan.inclusive");
        const std::string finished = state.temp("range.scan.finished");
        const std::string index = state.temp("range.scan.index");
        const std::string exclusiveBound = state.temp("range.scan.exclusive_bound");
        const std::string inclusiveBound = state.temp("range.scan.inclusive_bound");
        const std::string withinBound = state.temp("range.scan.within_bound");
        const std::string active = state.temp("range.scan.active");
        const std::string hasItem = state.temp("range.scan.has_item");
        const std::string atEnd = state.temp("range.scan.at_end");
        const std::string finalInclusiveItem = state.temp("range.scan.final_inclusive");
        const std::string prepareLabel = state.inlineLabel("range.scan.prepare");
        const std::string finalLabel = state.inlineLabel("range.scan.final");
        const std::string advanceLabel = state.inlineLabel("range.scan.advance");

        lines.push_back("  " + current + " = load i32, ptr " + iterState.rangeCurrentSlot + ", align 4");
        lines.push_back("  " + end + " = load i32, ptr " + iterState.rangeEndSlot + ", align 4");
        lines.push_back("  " + inclusive + " = load i1, ptr " + iterState.rangeInclusiveSlot + ", align 1");
        lines.push_back("  " + finished + " = load i1, ptr " + iterState.rangeFinishedSlot + ", align 1");
        lines.push_back("  " + index + " = load i64, ptr " + iterState.indexSlot + ", align 8");
        lines.push_back("  " + exclusiveBound + " = icmp slt i32 " + current + ", " + end);
        lines.push_back("  " + inclusiveBound + " = icmp sle i32 " + current + ", " + end);
        lines.push_back(
            "  " + withinBound + " = select i1 " + inclusive + ", i1 " + inclusiveBound + ", i1 " + exclusiveBound);
        lines.push_back("  " + active + " = icmp eq i1 " + finished + ", false");
        lines.push_back("  " + hasItem + " = and i1 " + active + ", " + withinBound);
        lines.push_back(
            "  br i1 " + hasItem + ", label %" + prepareLabel + ", label %" + blockLabel(value.exitLabel));
        lines.push_back(prepareLabel + ":");
        lines.push_back(
            "  store " + llvmType(value.itemType) + " " + current + ", ptr " + iterState.itemSlot +
            ", align " + std::to_string(iterState.itemAlign));
        if (!value.indexName.empty()) {
            if (iterState.visibleIndexSlot.empty()) {
                throw std::runtime_error("LLVM lowering could not allocate the indexed scan binding.");
            }
            lines.push_back("  store i64 " + index + ", ptr " + iterState.visibleIndexSlot + ", align 8");
        }
        lines.push_back("  " + atEnd + " = icmp eq i32 " + current + ", " + end);
        lines.push_back("  " + finalInclusiveItem + " = and i1 " + inclusive + ", " + atEnd);
        lines.push_back(
            "  br i1 " + finalInclusiveItem + ", label %" + finalLabel + ", label %" + advanceLabel);
        lines.push_back(finalLabel + ":");
        lines.push_back("  store i1 true, ptr " + iterState.rangeFinishedSlot + ", align 1");
        lines.push_back("  br label %" + blockLabel(value.bodyLabel));
        lines.push_back(advanceLabel + ":");
        const std::string nextCurrent = state.temp("range.scan.next_current");
        const std::string nextIndex = state.temp("range.scan.next_index");
        lines.push_back("  " + nextCurrent + " = add i32 " + current + ", 1");
        lines.push_back("  store i32 " + nextCurrent + ", ptr " + iterState.rangeCurrentSlot + ", align 4");
        lines.push_back("  " + nextIndex + " = add i64 " + index + ", 1");
        lines.push_back("  store i64 " + nextIndex + ", ptr " + iterState.indexSlot + ", align 8");
        lines.push_back("  br label %" + blockLabel(value.bodyLabel));
        state.pendingIterBindings[value.bodyLabel].push_back(
            PendingIterBinding{
                value.iterable,
                value.itemName,
                value.indexName,
                value.itemType,
                stateKey,
                value.itemBindingKey,
                value.indexBindingKey});
        return;
    }

    if (iterableBase != "Span" && iterableBase != "Str") {
        throw std::runtime_error(
            "LLVM lowering does not yet support iter_next over iterable type '" + value.iterable.type + "'.");
    }

    const std::string stateKey = std::string(currentBlockLabel) + "::" + value.itemName;
    IteratorState& iterState = ensureIteratorState(stateKey, value.itemName, value.itemType, state);
    const std::string iterableValue = ensureValue(value.iterable, state, lines);
    const std::string lengthValue = extractLength(iterableValue, value.iterable.type, state, lines);
    const std::string indexValue = state.temp("iter.index");
    lines.push_back("  " + indexValue + " = load i64, ptr " + iterState.indexSlot + ", align 8");
    const std::string hasItem = state.temp("iter.has_item");
    lines.push_back("  " + hasItem + " = icmp ult i64 " + indexValue + ", " + lengthValue);
    state.pendingIterBindings[value.bodyLabel].push_back(
        PendingIterBinding{
            value.iterable,
            value.itemName,
            value.indexName,
            value.itemType,
            stateKey,
            value.itemBindingKey,
            value.indexBindingKey});
    lines.push_back(
        "  br i1 " + hasItem + ", label %" + blockLabel(value.bodyLabel) + ", label %" + blockLabel(value.exitLabel));
}

void LlvmEmitter::emitBoundsCheck(const LirBoundsCheckInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const std::string receiverType = value.receiver.has_value()
        ? value.receiver->type
        : valueTypeForName(value.subject, state);
    if (receiverType.empty()) {
        throw std::runtime_error("LLVM lowering could not resolve bounds-check receiver '" + value.subject + "'.");
    }

    const std::string receiverValue = value.receiver.has_value()
        ? ensureValue(*value.receiver, state, lines)
        : ensureNamedValue(value.subject, state, lines);
    const std::string lengthValue = extractLength(receiverValue, receiverType, state, lines);
    const std::string failLabel = blockLabel(value.failLabel);
    const std::string continueLabel = state.inlineLabel("bounds.ok");

    if (value.kind == "byte_at") {
        if (value.args.size() != 1) {
            throw std::runtime_error("LLVM lowering expected one argument for byte_at bounds check.");
        }
        const std::string indexValue = ensureValue(value.args[0], state, lines);
        const std::string checkValue = state.temp("bounds.byte_at");
        lines.push_back("  " + checkValue + " = icmp ult i64 " + indexValue + ", " + lengthValue);
        lines.push_back("  br i1 " + checkValue + ", label %" + continueLabel + ", label %" + failLabel);
        lines.push_back(continueLabel + ":");
        return;
    }

    if (value.kind == "first_byte" || value.kind == "last_byte") {
        const std::string checkValue = state.temp("bounds.non_empty");
        lines.push_back("  " + checkValue + " = icmp ne i64 " + lengthValue + ", 0");
        lines.push_back("  br i1 " + checkValue + ", label %" + continueLabel + ", label %" + failLabel);
        lines.push_back(continueLabel + ":");
        return;
    }

    if (value.kind == "slice") {
        if (value.args.size() != 2) {
            throw std::runtime_error("LLVM lowering expected two arguments for slice bounds check.");
        }
        const std::string startValue = ensureValue(value.args[0], state, lines);
        const std::string countValue = ensureValue(value.args[1], state, lines);
        const std::string startOkValue = state.temp("bounds.slice.start");
        const std::string startOkLabel = state.inlineLabel("bounds.slice.start_ok");
        lines.push_back("  " + startOkValue + " = icmp ule i64 " + startValue + ", " + lengthValue);
        lines.push_back("  br i1 " + startOkValue + ", label %" + startOkLabel + ", label %" + failLabel);
        lines.push_back(startOkLabel + ":");
        const std::string remainingValue = state.temp("bounds.slice.remaining");
        lines.push_back("  " + remainingValue + " = sub i64 " + lengthValue + ", " + startValue);
        const std::string countOkValue = state.temp("bounds.slice.count");
        lines.push_back("  " + countOkValue + " = icmp ule i64 " + countValue + ", " + remainingValue);
        lines.push_back("  br i1 " + countOkValue + ", label %" + continueLabel + ", label %" + failLabel);
        lines.push_back(continueLabel + ":");
        return;
    }

    throw std::runtime_error("LLVM lowering does not yet support bounds_check kind '" + value.kind + "'.");
}

void LlvmEmitter::emitSwitch(const LirSwitchInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const auto choice = resolveChoiceType(value.value.type);
    if (!choice.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support switch over choice type '" + value.value.type + "'.");
    }

    const std::string switchValue = ensureValue(value.value, state, lines);
    const std::string tagValue = state.temp("choice.tag");
    lines.push_back("  " + tagValue + " = extractvalue " + choice->llvmTypeText + " " + switchValue + ", 0");
    const auto wildcard = std::find_if(value.cases.begin(), value.cases.end(), [](const LirSwitchCase& item) {
        return item.tag == "_";
    });
    const std::string fallbackLabel = wildcard == value.cases.end() ? value.defaultLabel : wildcard->targetLabel;
    lines.push_back("  switch i32 " + tagValue + ", label %" + blockLabel(fallbackLabel) + " [");

    for (const auto& item : value.cases) {
        if (item.tag == "_") {
            continue;
        }
        size_t tagIndex = choice->cases.size();
        for (size_t i = 0; i < choice->cases.size(); ++i) {
            if (choice->cases[i].name == item.tag) {
                tagIndex = i;
                break;
            }
        }
        if (tagIndex == choice->cases.size()) {
            throw std::runtime_error("LLVM lowering could not resolve switch tag '" + item.tag + "' in choice '" + value.value.type + "'.");
        }

        if (!item.bindings.empty()) {
            state.pendingChoiceBindings[item.targetLabel].push_back(
                PendingChoiceBinding{value.value, item.tag, item.bindings, item.bindingKeys});
        }
        lines.push_back("    i32 " + std::to_string(tagIndex) + ", label %" + blockLabel(item.targetLabel));
    }

    lines.push_back("  ]");
}

bool LlvmEmitter::emitTry(const LirTryInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const auto choice = resolveChoiceType(value.value.type);
    if (!choice.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support try over type '" + value.value.type + "'.");
    }

    const auto okIt = std::find_if(choice->cases.begin(), choice->cases.end(), [](const ConcreteChoiceCaseInfo& item) {
        return sameBackendIdentifier(item.name, "Ok");
    });
    const auto failIt = std::find_if(choice->cases.begin(), choice->cases.end(), [](const ConcreteChoiceCaseInfo& item) {
        return sameBackendIdentifier(item.name, "Fail");
    });
    if (okIt == choice->cases.end() || failIt == choice->cases.end()) {
        throw std::runtime_error("LLVM lowering requires try choices to define Ok(...) and Fail(...) variants for type '" + value.value.type + "'.");
    }
    if (okIt->payloadTypes.size() != 1 || failIt->payloadTypes.size() != 1) {
        throw std::runtime_error("LLVM lowering currently requires try variants Ok(...) and Fail(...) to carry exactly one payload each for type '" + value.value.type + "'.");
    }

    const std::string outcomeValue = ensureValue(value.value, state, lines);
    const std::string tagValue = state.temp("try.tag");
    lines.push_back("  " + tagValue + " = extractvalue " + choice->llvmTypeText + " " + outcomeValue + ", 0");

    const size_t okIndex = static_cast<size_t>(std::distance(choice->cases.begin(), okIt));
    const std::string checkValue = state.temp("try.is_ok");
    const std::string successLabel = value.successLabel.empty() ? state.inlineLabel("try.ok") : blockLabel(value.successLabel);
    const std::string failLabel = value.autoPropagate
        ? state.inlineLabel("try.fail")
        : blockLabel(value.failLabel);
    lines.push_back("  " + checkValue + " = icmp eq i32 " + tagValue + ", " + std::to_string(okIndex));
    lines.push_back("  br i1 " + checkValue + ", label %" + successLabel + ", label %" + failLabel);

    if (value.autoPropagate) {
        const auto returnChoice = resolveChoiceType(state.function.returnType);
        if (!returnChoice.has_value()) {
            throw std::runtime_error("LLVM lowering requires `try` shorthand functions to return Result-compatible types.");
        }
        const auto returnFailIt = std::find_if(returnChoice->cases.begin(), returnChoice->cases.end(), [](const ConcreteChoiceCaseInfo& item) {
            return sameBackendIdentifier(item.name, "Fail");
        });
        if (returnFailIt == returnChoice->cases.end() || returnFailIt->payloadTypes.size() != 1) {
            throw std::runtime_error("LLVM lowering requires propagated Result types to define Fail(E) with a single payload.");
        }

        const std::string failTempName = "__try_fail_" + std::to_string(state.nextInlineBlock++);
        lines.push_back(failLabel + ":");
        materializeChoiceBinding(PendingChoiceBinding{value.value, failIt->name, {failTempName}}, state, lines);

        const std::string returnAddr = state.temp("try.fail.ret.addr");
        const auto returnLayout = abiLayoutForType(state.function.returnType);
        const size_t returnAlign = returnLayout.has_value() ? returnLayout->align : 1;
        lines.push_back("  " + returnAddr + " = alloca " + llvmType(state.function.returnType) + ", align " + std::to_string(returnAlign));
        constructChoiceValueAtAddress(
            returnAddr,
            state.function.returnType,
            returnFailIt->name,
            {LirValue{failTempName, returnFailIt->payloadTypes[0], false}},
            state,
            lines);

        if (state.returnSlot.has_value()) {
            const std::string operand = loadValueFromAddress(returnAddr, state.function.returnType, returnAlign, state, lines);
            storeValueToAddress(*state.returnSlot, state.function.returnType, operand, returnAlign, lines);
            lines.push_back("  ret void");
        } else {
            const std::string operand = loadValueFromAddress(returnAddr, state.function.returnType, returnAlign, state, lines);
            lines.push_back("  ret " + llvmType(state.function.returnType) + " " + operand);
        }
    } else {
        state.pendingChoiceBindings[value.failLabel].push_back(PendingChoiceBinding{
            value.value, failIt->name, {value.failName}, {value.failBindingKey}});
    }

    if (!value.successLabel.empty()) {
        state.pendingChoiceBindings[value.successLabel].push_back(PendingChoiceBinding{
            value.value, okIt->name, {value.okName}, {value.okBindingKey}});
        return true;
    }

    lines.push_back(successLabel + ":");
    materializeChoiceBinding(
        PendingChoiceBinding{value.value, okIt->name, {value.okName}, {value.okBindingKey}}, state, lines);
    return false;
}

void LlvmEmitter::emitBuiltinCall(const LirCallInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const std::string builtinTag = value.builtinTag.value_or(tailSegment(value.callee));
    if (builtinTag == "Anchor.new") {
        if (value.args.size() != 1) {
            throw std::runtime_error("LLVM lowering expected one argument for Anchor.new.");
        }
        if (!value.result.has_value()) {
            return;
        }

        const ParsedTypeName anchorType = parseTypeName(value.type);
        if (anchorType.base != "Anchor" || anchorType.args.size() != 1) {
            throw std::runtime_error("LLVM lowering expected Anchor[T] result type for Anchor.new, got '" + value.type + "'.");
        }

        const std::string payloadType = anchorType.args.front();
        const auto payloadLayout = abiLayoutForType(payloadType);
        if (!payloadLayout.has_value()) {
            throw std::runtime_error("LLVM lowering does not yet support Anchor payload type '" + payloadType + "'.");
        }

        const std::string allocSymbol = quoteGlobal("claw.runtime.anchor.alloc");
        addRuntimeDecl("declare ptr " + allocSymbol + "(i64)");

        const std::string allocReg = state.temp("anchor.alloc");
        const size_t payloadBytes = std::max<size_t>(payloadLayout->size, 1);
        lines.push_back("  " + allocReg + " = call ptr " + allocSymbol + "(i64 " + std::to_string(payloadBytes) + ")");

        if (payloadLayout->size > 0 && llvmType(payloadType) != "void") {
            const std::string operand = ensureValue(value.args.front(), state, lines);
            storeValueToAddress(allocReg, payloadType, operand, payloadLayout->align, lines);
        }

        state.namedValues[*value.result] = allocReg;
        state.namedAddresses.erase(*value.result);
        state.params.erase(*value.result);
        state.valueTypes[*value.result] = value.type;
        return;
    }

    if (builtinTag == "Arena.new") {
        if (!value.args.empty()) {
            throw std::runtime_error("LLVM lowering expected zero arguments for Arena.new.");
        }
        if (!value.result.has_value()) {
            return;
        }

        const ParsedTypeName arenaType = parseTypeName(value.type);
        if (arenaType.base != "Arena") {
            throw std::runtime_error("LLVM lowering expected Arena result type for Arena.new, got '" + value.type + "'.");
        }

        const std::string allocSymbol = quoteGlobal("claw.runtime.arena.new");
        addRuntimeDecl("declare ptr " + allocSymbol + "()");

        const std::string allocReg = state.temp("arena.new");
        lines.push_back("  " + allocReg + " = call ptr " + allocSymbol + "()");

        state.namedValues[*value.result] = allocReg;
        state.namedAddresses.erase(*value.result);
        state.params.erase(*value.result);
        state.valueTypes[*value.result] = value.type;
        return;
    }

    const std::string methodName = tailSegment(builtinTag);
    const auto storeResultType = [&]() {
        if (value.result.has_value()) {
            state.valueTypes[*value.result] = value.type;
        }
    };

    if (!value.builtinReceiver.has_value()) {
        throw std::runtime_error("LLVM lowering expected an explicit builtin receiver value for '" + value.callee + "'.");
    }

    const LirValue& receiver = *value.builtinReceiver;
    const std::string receiverType = receiver.type;
    if (receiverType.empty()) {
        throw std::runtime_error("LLVM lowering could not resolve builtin receiver type for '" + receiver.text + "'.");
    }

    const std::string receiverValue = ensureValue(receiver, state, lines);
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(receiverType)));

    if (baseType == "Anchor" && methodName == "get") {
        if (!value.result.has_value()) {
            return;
        }
        state.namedAddresses[*value.result] = receiverValue;
        state.namedValues.erase(*value.result);
        state.params.erase(*value.result);
        storeResultType();
        return;
    }

    if (baseType == "Arena" && methodName == "alloc") {
        if (value.args.size() != 1) {
            throw std::runtime_error("LLVM lowering expected one argument for Arena.alloc.");
        }
        
        const std::string payloadType = value.args.front().type;
        const auto payloadLayout = abiLayoutForType(payloadType);
        if (!payloadLayout.has_value()) {
            throw std::runtime_error("LLVM lowering does not yet support Arena payload type '" + payloadType + "'.");
        }
        
        const std::string allocSymbol = quoteGlobal("claw.runtime.arena.alloc");
        addRuntimeDecl("declare ptr " + allocSymbol + "(ptr, i64, i64)");
        
        const size_t payloadBytes = std::max<size_t>(payloadLayout->size, 1);
        const size_t alignBytes = std::max<size_t>(payloadLayout->align, 1);
        
        const std::string allocReg = state.temp("arena.alloc");
        lines.push_back("  " + allocReg + " = call ptr " + allocSymbol + "(ptr " + receiverValue + ", i64 " + std::to_string(payloadBytes) + ", i64 " + std::to_string(alignBytes) + ")");

        if (payloadLayout->size > 0 && llvmType(payloadType) != "void") {
            const std::string operand = ensureValue(value.args.front(), state, lines);
            storeValueToAddress(allocReg, payloadType, operand, payloadLayout->align, lines);
        }

        if (value.result.has_value()) {
            state.namedAddresses[*value.result] = allocReg;
            state.namedValues.erase(*value.result);
            state.params.erase(*value.result);
            storeResultType();
        }
        return;
    }

    if (baseType == "Arena" && methodName == "reset") {
        const std::string resetSymbol = quoteGlobal("claw.runtime.arena.reset");
        addRuntimeDecl("declare void " + resetSymbol + "(ptr)");
        lines.push_back("  call void " + resetSymbol + "(ptr " + receiverValue + ")");
        return;
    }

    if (methodName == "len" && (baseType == "Str" || baseType == "Span" || baseType == "Vec")) {
        if (!value.result.has_value()) {
            return;
        }
        const std::string resultReg = localName(*value.result);
        const std::string lengthValue = extractLength(receiverValue, receiverType, state, lines);
        lines.push_back("  " + resultReg + " = add i64 0, " + lengthValue);
        storeResultType();
        return;
    }

    if (methodName == "is_empty" && (baseType == "Str" || baseType == "Span" || baseType == "Vec")) {
        if (!value.result.has_value()) {
            return;
        }
        const std::string resultReg = localName(*value.result);
        const std::string lengthValue = extractLength(receiverValue, receiverType, state, lines);
        lines.push_back("  " + resultReg + " = icmp eq i64 " + lengthValue + ", 0");
        storeResultType();
        return;
    }

    if (methodName == "capacity" && baseType == "Vec") {
        if (!value.result.has_value()) {
            return;
        }
        const std::string resultReg = localName(*value.result);
        const std::string capReg = state.temp("vec.cap");
        lines.push_back("  " + capReg + " = extractvalue %claw.buffer " + receiverValue + ", 2");
        lines.push_back("  " + resultReg + " = add i64 0, " + capReg);
        storeResultType();
        return;
    }

    if (builtinTag == "Str.byte_at" || (baseType == "Str" && methodName == "byte_at")) {
        if (value.args.size() != 1) {
            throw std::runtime_error("LLVM lowering expected one argument for text.byte_at.");
        }
        if (!value.result.has_value()) {
            return;
        }
        const std::string indexValue = ensureValue(value.args[0], state, lines);
        const std::string pointerValue = extractPointer(receiverValue, receiverType, state, lines);
        const std::string elementPtr = state.temp("text.byte_at.ptr");
        lines.push_back("  " + elementPtr + " = getelementptr inbounds i8, ptr " + pointerValue + ", i64 " + indexValue);
        lines.push_back("  " + localName(*value.result) + " = load i8, ptr " + elementPtr);
        storeResultType();
        return;
    }

    if (builtinTag == "Str.first_byte" || (baseType == "Str" && methodName == "first_byte")) {
        if (!value.result.has_value()) {
            return;
        }
        const std::string pointerValue = extractPointer(receiverValue, receiverType, state, lines);
        lines.push_back("  " + localName(*value.result) + " = load i8, ptr " + pointerValue);
        storeResultType();
        return;
    }

    if (builtinTag == "Str.last_byte" || (baseType == "Str" && methodName == "last_byte")) {
        if (!value.result.has_value()) {
            return;
        }
        const std::string lengthValue = extractLength(receiverValue, receiverType, state, lines);
        const std::string indexValue = state.temp("text.last_byte.index");
        lines.push_back("  " + indexValue + " = sub i64 " + lengthValue + ", 1");
        const std::string pointerValue = extractPointer(receiverValue, receiverType, state, lines);
        const std::string elementPtr = state.temp("text.last_byte.ptr");
        lines.push_back("  " + elementPtr + " = getelementptr inbounds i8, ptr " + pointerValue + ", i64 " + indexValue);
        lines.push_back("  " + localName(*value.result) + " = load i8, ptr " + elementPtr);
        storeResultType();
        return;
    }

    if (builtinTag == "Str.slice" || (baseType == "Str" && methodName == "slice")) {
        if (value.args.size() != 2) {
            throw std::runtime_error("LLVM lowering expected two arguments for text.slice.");
        }
        if (!value.result.has_value()) {
            return;
        }
        const std::string startValue = ensureValue(value.args[0], state, lines);
        const std::string countValue = ensureValue(value.args[1], state, lines);
        const std::string pointerValue = extractPointer(receiverValue, receiverType, state, lines);
        const std::string slicePtr = state.temp("text.slice.ptr");
        lines.push_back("  " + slicePtr + " = getelementptr inbounds i8, ptr " + pointerValue + ", i64 " + startValue);
        const std::string sliceBase = state.temp("text.slice.base");
        lines.push_back("  " + sliceBase + " = insertvalue %claw.slice poison, ptr " + slicePtr + ", 0");
        lines.push_back("  " + localName(*value.result) + " = insertvalue %claw.slice " + sliceBase + ", i64 " + countValue + ", 1");
        storeResultType();
        return;
    }

    if (baseType == "Str" &&
        (methodName == "starts_with" || methodName == "ends_with" || methodName == "contains")) {
        if (value.args.size() != 1) {
            throw std::runtime_error("LLVM lowering expected one argument for Str." + methodName + ".");
        }
        if (!value.result.has_value()) {
            return;
        }
        const std::string receiverAddress = ensureAddress(receiver, state, lines);
        const std::string argumentAddress = ensureAddress(value.args.front(), state, lines);
        const std::string symbol = quoteGlobal("claw.runtime.str." + methodName);
        addRuntimeDecl("declare i1 " + symbol + "(ptr, ptr)");
        lines.push_back(
            "  " + localName(*value.result) + " = call i1 " + symbol +
            "(ptr " + receiverAddress + ", ptr " + argumentAddress + ")");
        storeResultType();
        return;
    }

    throw std::runtime_error("LLVM lowering does not yet support builtin method '" + builtinTag + "'.");
}
} // namespace claw::codegen
