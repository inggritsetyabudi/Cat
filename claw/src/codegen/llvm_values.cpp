#include "codegen/llvm_internal.h"
#include "analysis/numeric_literals.h"

#include <bit>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace claw::codegen {
namespace {

std::string llvmFloatConstant(std::string_view rawText, std::string_view typeName) {
    std::string text = numeric_literals::splitNumericLiteralText(std::string(rawText)).magnitude;
    const auto integerLiteral = numeric_literals::parseIntegerLiteralText(std::string(rawText));
    if (integerLiteral.valid) {
        text = integerLiteral.magnitude;
    }
    std::uint64_t bits = 0;
    if (typeName == "Float32") {
        float value = 0.0f;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
            throw std::runtime_error("LLVM lowering could not parse Float32 literal '" + text + "'.");
        }
        bits = std::bit_cast<std::uint64_t>(static_cast<double>(value));
    } else if (typeName == "Float64") {
        double value = 0.0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
            throw std::runtime_error("LLVM lowering could not parse Float64 literal '" + text + "'.");
        }
        bits = std::bit_cast<std::uint64_t>(value);
    } else {
        throw std::runtime_error("LLVM lowering does not support floating literal type '" + std::string(typeName) + "'.");
    }

    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << bits;
    return out.str();
}

} // namespace

void LlvmEmitter::materializeChoiceBinding(
    const PendingChoiceBinding& binding,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const auto choice = resolveChoiceType(binding.source.type);
    if (!choice.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support choice bindings for type '" + binding.source.type + "'.");
    }

    const auto caseIt = std::find_if(choice->cases.begin(), choice->cases.end(), [&](const ConcreteChoiceCaseInfo& item) {
        return item.name == binding.variantName;
    });
    if (caseIt == choice->cases.end()) {
        throw std::runtime_error("LLVM lowering could not resolve choice variant '" + binding.variantName + "'.");
    }
    if (binding.bindingNames.size() != caseIt->payloadTypes.size() ||
        binding.bindingNames.size() != caseIt->layout.payloadFields.size()) {
        throw std::runtime_error("LLVM lowering found mismatched payload binding information for variant '" + binding.variantName + "'.");
    }
    if (!binding.bindingKeys.empty() && binding.bindingKeys.size() != binding.bindingNames.size()) {
        throw std::runtime_error("LLVM lowering found mismatched payload binding identities for variant '" + binding.variantName + "'.");
    }
    if (binding.bindingNames.empty()) {
        return;
    }
    if (choice->payloadSizeBytes == 0) {
        throw std::runtime_error("LLVM lowering expected payload storage for bound choice variant '" + binding.variantName + "'.");
    }

    const std::string sourceValue = ensureValue(binding.source, state, lines);
    const std::string choiceAddr = state.temp("choice.case.addr");
    lines.push_back("  " + choiceAddr + " = alloca " + choice->llvmTypeText + ", align " + std::to_string(choice->alignBytes));
    lines.push_back("  store " + choice->llvmTypeText + " " + sourceValue + ", ptr " + choiceAddr + ", align " + std::to_string(choice->alignBytes));

    const size_t paddingBytes = choice->payloadOffsetBytes > choice->tagSizeBytes
        ? choice->payloadOffsetBytes - choice->tagSizeBytes
        : 0;
    const int payloadFieldIndex = paddingBytes > 0 ? 2 : 1;
    const std::string payloadArrayType = "[" + std::to_string(choice->payloadSizeBytes) + " x i8]";
    const std::string payloadSlot = state.temp("choice.payload.slot");
    lines.push_back(
        "  " + payloadSlot + " = getelementptr inbounds " + choice->llvmTypeText + ", ptr " + choiceAddr +
        ", i32 0, i32 " + std::to_string(payloadFieldIndex));
    const std::string payloadBase = state.temp("choice.payload.base");
    lines.push_back("  " + payloadBase + " = getelementptr inbounds " + payloadArrayType + ", ptr " + payloadSlot + ", i32 0, i64 0");

    for (size_t i = 0; i < binding.bindingNames.size(); ++i) {
        const auto& layoutField = caseIt->layout.payloadFields[i];
        const std::string fieldPtr = state.temp("choice.payload.field.ptr");
        lines.push_back(
            "  " + fieldPtr + " = getelementptr inbounds i8, ptr " + payloadBase +
            ", i64 " + std::to_string(layoutField.offsetBytes));
        const std::string loadedValue = state.temp("choice.payload.field");
        lines.push_back(
            "  " + loadedValue + " = load " + llvmType(caseIt->payloadTypes[i]) + ", ptr " + fieldPtr +
            ", align " + std::to_string(layoutField.alignBytes));
        const std::string& bindingKey =
            i < binding.bindingKeys.size() && !binding.bindingKeys[i].empty()
                ? binding.bindingKeys[i]
                : binding.bindingNames[i];
        state.namedValues[bindingKey] = loadedValue;
        state.namedAddresses.erase(bindingKey);
        state.valueTypes[bindingKey] = caseIt->payloadTypes[i];
    }
}

IteratorState& LlvmEmitter::ensureIteratorState(
    std::string_view stateKey,
    std::string_view itemName,
    std::string_view itemType,
    FunctionState& state) {
    const std::string key(stateKey);
    const auto it = state.iteratorStates.find(key);
    if (it != state.iteratorStates.end()) {
        return it->second;
    }

    const auto layout = abiLayoutForType(itemType);
    if (!layout.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support iterator item type '" + std::string(itemType) + "'.");
    }

    IteratorState iterState;
    iterState.indexSlot = state.temp(std::string(itemName) + ".iter.index.addr");
    iterState.itemSlot = state.temp(std::string(itemName) + ".iter.item.addr");
    iterState.itemAlign = layout->align;

    state.entryAllocas.push_back("  " + iterState.indexSlot + " = alloca i64, align 8");
    state.entryAllocas.push_back("  store i64 0, ptr " + iterState.indexSlot + ", align 8");
    state.entryAllocas.push_back(
        "  " + iterState.itemSlot + " = alloca " + llvmType(std::string(itemType)) + ", align " + std::to_string(iterState.itemAlign));

    const auto [insertedIt, _] = state.iteratorStates.emplace(key, std::move(iterState));
    return insertedIt->second;
}

IteratorState& LlvmEmitter::ensureRangeIteratorState(
    std::string_view stateKey,
    std::string_view itemName,
    std::string_view indexName,
    std::string_view itemType,
    FunctionState& state) {
    const std::string key(stateKey);
    const auto it = state.iteratorStates.find(key);
    if (it != state.iteratorStates.end()) {
        return it->second;
    }

    const auto layout = abiLayoutForType(itemType);
    if (!layout.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support iterator item type '" + std::string(itemType) + "'.");
    }

    IteratorState iterState;
    iterState.isRange = true;
    iterState.itemAlign = layout->align;
    iterState.indexSlot = state.temp(std::string(itemName) + ".range.index.addr");
    iterState.itemSlot = state.temp(std::string(itemName) + ".range.item.addr");
    iterState.rangeCurrentSlot = state.temp(std::string(itemName) + ".range.current.addr");
    iterState.rangeEndSlot = state.temp(std::string(itemName) + ".range.end.addr");
    iterState.rangeInclusiveSlot = state.temp(std::string(itemName) + ".range.inclusive.addr");
    iterState.rangeFinishedSlot = state.temp(std::string(itemName) + ".range.finished.addr");
    if (!indexName.empty()) {
        iterState.visibleIndexSlot = state.temp(std::string(indexName) + ".range.visible_index.addr");
    }

    state.entryAllocas.push_back("  " + iterState.indexSlot + " = alloca i64, align 8");
    state.entryAllocas.push_back("  store i64 0, ptr " + iterState.indexSlot + ", align 8");
    state.entryAllocas.push_back(
        "  " + iterState.itemSlot + " = alloca " + llvmType(std::string(itemType)) + ", align " +
        std::to_string(iterState.itemAlign));
    state.entryAllocas.push_back("  " + iterState.rangeCurrentSlot + " = alloca i32, align 4");
    state.entryAllocas.push_back("  " + iterState.rangeEndSlot + " = alloca i32, align 4");
    state.entryAllocas.push_back("  " + iterState.rangeInclusiveSlot + " = alloca i1, align 1");
    state.entryAllocas.push_back("  " + iterState.rangeFinishedSlot + " = alloca i1, align 1");
    state.entryAllocas.push_back("  store i1 false, ptr " + iterState.rangeFinishedSlot + ", align 1");
    if (!iterState.visibleIndexSlot.empty()) {
        state.entryAllocas.push_back("  " + iterState.visibleIndexSlot + " = alloca i64, align 8");
    }

    const auto [insertedIt, _] = state.iteratorStates.emplace(key, std::move(iterState));
    return insertedIt->second;
}

void LlvmEmitter::materializeIterBinding(
    const PendingIterBinding& binding,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const bool isRange = binding.iterable.type == "Range[Int32]" || binding.iterable.type == "Range[Char]";
    const std::string itemKey = binding.itemBindingKey.empty() ? binding.itemName : binding.itemBindingKey;
    const std::string indexKey = binding.indexBindingKey.empty() ? binding.indexName : binding.indexBindingKey;
    IteratorState& iterState = isRange
        ? ensureRangeIteratorState(binding.stateKey, binding.itemName, binding.indexName, binding.itemType, state)
        : ensureIteratorState(binding.stateKey, binding.itemName, binding.itemType, state);
    if (isRange) {
        state.namedAddresses[itemKey] = iterState.itemSlot;
        state.namedValues.erase(itemKey);
        state.params.erase(itemKey);
        state.valueTypes[itemKey] = binding.itemType;
        if (!binding.indexName.empty()) {
            if (iterState.visibleIndexSlot.empty()) {
                throw std::runtime_error("LLVM lowering could not allocate the indexed scan binding.");
            }
            state.namedAddresses[indexKey] = iterState.visibleIndexSlot;
            state.namedValues.erase(indexKey);
            state.params.erase(indexKey);
            state.valueTypes[indexKey] = "USize";
        }
        return;
    }

    const auto layout = abiLayoutForType(binding.itemType);
    if (!layout.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support iterator item type '" + binding.itemType + "'.");
    }

    const std::string iterableValue = ensureValue(binding.iterable, state, lines);
    const std::string indexValue = state.temp("iter.index");
    lines.push_back("  " + indexValue + " = load i64, ptr " + iterState.indexSlot + ", align 8");

    const std::string pointerValue = extractPointer(iterableValue, binding.iterable.type, state, lines);
    const std::string elementPtr = state.temp("iter.item.ptr");
    lines.push_back(
        "  " + elementPtr + " = getelementptr inbounds " + llvmType(binding.itemType) + ", ptr " + pointerValue +
        ", i64 " + indexValue);
    const std::string loadedItem = state.temp("iter.item");
    lines.push_back(
        "  " + loadedItem + " = load " + llvmType(binding.itemType) + ", ptr " + elementPtr +
        ", align " + std::to_string(layout->align));
    lines.push_back(
        "  store " + llvmType(binding.itemType) + " " + loadedItem + ", ptr " + iterState.itemSlot +
        ", align " + std::to_string(layout->align));

    const std::string nextIndex = state.temp("iter.next.index");
    lines.push_back("  " + nextIndex + " = add i64 " + indexValue + ", 1");
    lines.push_back("  store i64 " + nextIndex + ", ptr " + iterState.indexSlot + ", align 8");

    state.namedAddresses[itemKey] = iterState.itemSlot;
    state.namedValues.erase(itemKey);
    state.params.erase(itemKey);
    state.valueTypes[itemKey] = binding.itemType;
}

void LlvmEmitter::materializePendingBlockBindings(
    std::string_view blockLabel,
    FunctionState& state,
    std::vector<std::string>& lines) {
    if (const auto it = state.pendingIterBindings.find(std::string(blockLabel)); it != state.pendingIterBindings.end()) {
        for (const auto& binding : it->second) {
            materializeIterBinding(binding, state, lines);
        }
    }

    const auto it = state.pendingChoiceBindings.find(std::string(blockLabel));
    if (it == state.pendingChoiceBindings.end()) {
        return;
    }

    for (const auto& binding : it->second) {
        materializeChoiceBinding(binding, state, lines);
    }
}

void LlvmEmitter::constructChoiceValueAtAddress(
    std::string_view address,
    const std::string& typeText,
    std::string_view variantName,
    const std::vector<LirValue>& payloads,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const auto choice = resolveChoiceType(typeText);
    if (!choice.has_value()) {
        throw std::runtime_error("LLVM lowering could not resolve choice construction type '" + typeText + "'.");
    }

    const auto caseIt = std::find_if(choice->cases.begin(), choice->cases.end(), [&](const ConcreteChoiceCaseInfo& item) {
        return sameBackendIdentifier(item.name, variantName);
    });
    if (caseIt == choice->cases.end()) {
        throw std::runtime_error("LLVM lowering could not resolve choice constructor variant '" + std::string(variantName) + "'.");
    }
    if (payloads.size() != caseIt->payloadTypes.size()) {
        throw std::runtime_error("LLVM lowering found mismatched payload count for choice constructor '" + std::string(variantName) + "'.");
    }

    lines.push_back(
        "  store " + choice->llvmTypeText + " zeroinitializer, ptr " + std::string(address) +
        ", align " + std::to_string(choice->alignBytes));

    const size_t caseIndex = static_cast<size_t>(std::distance(choice->cases.begin(), caseIt));
    const std::string tagPtr = state.temp("choice.tag.ptr");
    lines.push_back(
        "  " + tagPtr + " = getelementptr inbounds " + choice->llvmTypeText + ", ptr " + std::string(address) +
        ", i32 0, i32 0");
    lines.push_back("  store i32 " + std::to_string(caseIndex) + ", ptr " + tagPtr + ", align " + std::to_string(choice->tagSizeBytes));

    if (payloads.empty()) {
        return;
    }

    if (choice->payloadSizeBytes == 0) {
        throw std::runtime_error("LLVM lowering expected payload storage for constructor variant '" + std::string(variantName) + "'.");
    }

    const size_t paddingBytes = choice->payloadOffsetBytes > choice->tagSizeBytes
        ? choice->payloadOffsetBytes - choice->tagSizeBytes
        : 0;
    const int payloadFieldIndex = paddingBytes > 0 ? 2 : 1;
    const std::string payloadArrayType = "[" + std::to_string(choice->payloadSizeBytes) + " x i8]";
    const std::string payloadSlot = state.temp("choice.payload.slot");
    lines.push_back(
        "  " + payloadSlot + " = getelementptr inbounds " + choice->llvmTypeText + ", ptr " + std::string(address) +
        ", i32 0, i32 " + std::to_string(payloadFieldIndex));
    const std::string payloadBase = state.temp("choice.payload.base");
    lines.push_back("  " + payloadBase + " = getelementptr inbounds " + payloadArrayType + ", ptr " + payloadSlot + ", i32 0, i64 0");

    for (size_t i = 0; i < payloads.size(); ++i) {
        const auto& layoutField = caseIt->layout.payloadFields[i];
        const std::string fieldPtr = state.temp("choice.payload.field.ptr");
        lines.push_back(
            "  " + fieldPtr + " = getelementptr inbounds i8, ptr " + payloadBase +
            ", i64 " + std::to_string(layoutField.offsetBytes));
        const std::string operand = ensureValue(payloads[i], state, lines);
        storeValueToAddress(fieldPtr, caseIt->payloadTypes[i], operand, layoutField.alignBytes, lines);
    }
}

void LlvmEmitter::emitChoiceMakeInst(const LirChoiceMakeInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const auto layout = abiLayoutForType(value.type);
    const size_t align = layout.has_value() ? layout->align : 1;
    const std::string resultAddr = state.temp(value.result + ".choice.addr");
    lines.push_back("  " + resultAddr + " = alloca " + llvmType(value.type) + ", align " + std::to_string(align));
    constructChoiceValueAtAddress(resultAddr, value.type, value.variantName, value.payloads, state, lines);
    state.namedAddresses[value.result] = resultAddr;
    state.namedValues.erase(value.result);
    state.params.erase(value.result);
    state.valueTypes[value.result] = value.type;
}

std::string LlvmEmitter::valueTypeForName(std::string_view name, const FunctionState& state) const {
    const auto it = state.valueTypes.find(std::string(name));
    return it != state.valueTypes.end() ? it->second : std::string();
}

std::string LlvmEmitter::ensureNamedValue(std::string_view name, FunctionState& state, std::vector<std::string>& lines) {
    const std::string type = valueTypeForName(name, state);
    if (type.empty()) {
        throw std::runtime_error("LLVM lowering could not resolve type for value '" + std::string(name) + "'.");
    }
    return ensureValue(LirValue{std::string(name), type, false}, state, lines);
}

std::optional<std::string> LlvmEmitter::tryAddressOfValue(
    const LirValue& value,
    FunctionState& state,
    std::vector<std::string>&) {
    const std::string& valueKey = value.bindingKey.empty() ? value.text : value.bindingKey;
    if (const auto it = state.stackPointers.find(valueKey); it != state.stackPointers.end()) {
        return it->second;
    }
    if (const auto it = state.namedAddresses.find(valueKey); it != state.namedAddresses.end()) {
        return it->second;
    }
    if (const auto it = staticsByName.find(value.text); it != staticsByName.end()) {
        return quoteGlobal(it->second->link.symbol);
    }
    return std::nullopt;
}

std::string LlvmEmitter::ensureAddress(
    const LirValue& value,
    FunctionState& state,
    std::vector<std::string>& lines) {
    if (const auto existing = tryAddressOfValue(value, state, lines); existing.has_value()) {
        return *existing;
    }

    const std::string tempAddr = state.temp("addr");
    const std::string valueType = llvmType(value.type);
    const auto layout = abiLayoutForType(value.type);
    const size_t align = layout.has_value() ? layout->align : 1;
    lines.push_back("  " + tempAddr + " = alloca " + valueType + ", align " + std::to_string(align));
    const std::string operand = ensureValue(value, state, lines);
    lines.push_back("  store " + valueType + " " + operand + ", ptr " + tempAddr + ", align " + std::to_string(align));

    const std::string& valueKey = value.bindingKey.empty() ? value.text : value.bindingKey;
    if (!valueKey.empty() && !isStringLiteral(value.text) && value.text != "true" && value.text != "false" && !isIntegerLiteral(value.text)) {
        state.namedAddresses[valueKey] = tempAddr;
        state.namedValues.erase(valueKey);
        state.params.erase(valueKey);
    }
    return tempAddr;
}

const LayoutFieldInfo* LlvmEmitter::findShapeFieldLayout(std::string_view typeText, std::string_view field) const {
    const TypeLayoutInfo* layout = resolveShapeLayout(typeText);
    if (layout == nullptr) {
        return nullptr;
    }
    for (const auto& item : layout->fields) {
        if (item.name == field) {
            return &item;
        }
    }
    return nullptr;
}

size_t LlvmEmitter::findShapeFieldIndex(std::string_view typeText, std::string_view field) const {
    const std::string baseType = parseTypeName(typeText).base;
    if (baseType == "Tuple") {
        const ParsedTypeName parsedType = parseTypeName(typeText);
        size_t index = 0;
        const auto parsedIndex = std::from_chars(field.data(), field.data() + field.size(), index);
        if (field.empty() || parsedIndex.ec != std::errc{} || parsedIndex.ptr != field.data() + field.size() ||
            index >= parsedType.args.size()) {
            throw std::runtime_error("LLVM lowering found invalid tuple projection '" + std::string(field) + "'.");
        }
        return index;
    }
    const auto indexMapIt = shapeFieldIndices.find(baseType);
    if (indexMapIt == shapeFieldIndices.end()) {
        throw std::runtime_error("LLVM lowering only supports field access on concrete shapes, got '" + std::string(typeText) + "'.");
    }
    const auto indexIt = indexMapIt->second.find(std::string(field));
    if (indexIt == indexMapIt->second.end()) {
        throw std::runtime_error("Unknown field '" + std::string(field) + "' during LLVM lowering.");
    }
    return indexIt->second;
}

std::string LlvmEmitter::loadValueFromAddress(
    std::string_view address,
    const std::string& typeText,
    size_t align,
    FunctionState& state,
    std::vector<std::string>& lines) const {
    const std::string loadReg = state.temp("load");
    std::string line = "  " + loadReg + " = load " + llvmType(typeText) + ", ptr " + std::string(address);
    if (align > 0) {
        line += ", align " + std::to_string(align);
    }
    lines.push_back(std::move(line));
    return loadReg;
}

void LlvmEmitter::storeValueToAddress(
    std::string_view address,
    const std::string& typeText,
    const std::string& operand,
    size_t align,
    std::vector<std::string>& lines) const {
    std::string line = "  store " + llvmType(typeText) + " " + operand + ", ptr " + std::string(address);
    if (align > 0) {
        line += ", align " + std::to_string(align);
    }
    lines.push_back(std::move(line));
}

std::string LlvmEmitter::emitFieldAddress(
    const LirValue& object,
    std::string_view field,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const std::string baseType = parseTypeName(object.type).base;
    const auto* layoutField = baseType == "Tuple" ? nullptr : findShapeFieldLayout(object.type, field);
    if (baseType != "Tuple" && layoutField == nullptr) {
        throw std::runtime_error("LLVM lowering only supports field access on concrete shapes, got '" + object.type + "'.");
    }

    const size_t fieldIndex = findShapeFieldIndex(object.type, field);
    const std::string objectAddress = ensureAddress(object, state, lines);
    const std::string fieldPtr = state.temp("field.ptr");
    lines.push_back(
        "  " + fieldPtr + " = getelementptr inbounds " + llvmType(object.type) + ", ptr " + objectAddress +
        ", i32 0, i32 " + std::to_string(fieldIndex));
    return fieldPtr;
}

std::string LlvmEmitter::extractLength(
    const std::string& aggregateValue,
    std::string_view receiverType,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(receiverType)));
    if (baseType == "Str" || baseType == "Span") {
        const std::string lengthReg = state.temp("str.len");
        lines.push_back("  " + lengthReg + " = extractvalue %claw.slice " + aggregateValue + ", 1");
        return lengthReg;
    }
    if (baseType == "Vec") {
        const std::string lengthReg = state.temp("vec.len");
        lines.push_back("  " + lengthReg + " = extractvalue %claw.buffer " + aggregateValue + ", 1");
        return lengthReg;
    }
    throw std::runtime_error("LLVM lowering does not yet support length extraction for receiver type '" + std::string(receiverType) + "'.");
}

std::string LlvmEmitter::extractPointer(
    const std::string& aggregateValue,
    std::string_view receiverType,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(receiverType)));
    if (baseType == "Str" || baseType == "Span") {
        const std::string pointerReg = state.temp("str.ptr");
        lines.push_back("  " + pointerReg + " = extractvalue %claw.slice " + aggregateValue + ", 0");
        return pointerReg;
    }
    if (baseType == "Vec") {
        const std::string pointerReg = state.temp("vec.ptr");
        lines.push_back("  " + pointerReg + " = extractvalue %claw.buffer " + aggregateValue + ", 0");
        return pointerReg;
    }
    throw std::runtime_error("LLVM lowering does not yet support pointer extraction for receiver type '" + std::string(receiverType) + "'.");
}

std::string LlvmEmitter::emitIndexAddress(
    const LirValue& object,
    const LirValue& index,
    const std::string& elementType,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(object.type)));
    if (baseType != "Str" && baseType != "Span" && baseType != "Vec") {
        throw std::runtime_error("LLVM lowering does not yet support index access on type '" + object.type + "'.");
    }

    const std::string objectValue = ensureValue(object, state, lines);
    const std::string lengthValue = extractLength(objectValue, object.type, state, lines);
    const std::string indexValue = ensureValue(index, state, lines);
    const std::string inBounds = state.temp("index.in_bounds");
    const std::string failLabel = state.inlineLabel("index.bounds_fail");
    const std::string continueLabel = state.inlineLabel("index.bounds_ok");
    lines.push_back("  " + inBounds + " = icmp ult i64 " + indexValue + ", " + lengthValue);
    lines.push_back("  br i1 " + inBounds + ", label %" + continueLabel + ", label %" + failLabel);
    lines.push_back(failLabel + ":");
    lines.push_back("  call void @llvm.trap()");
    lines.push_back("  unreachable");
    lines.push_back(continueLabel + ":");
    needsTrap = true;

    const std::string basePointer = extractPointer(objectValue, object.type, state, lines);
    const std::string elementPointer = state.temp("index.element_ptr");
    lines.push_back(
        "  " + elementPointer + " = getelementptr inbounds " + llvmType(elementType) +
        ", ptr " + basePointer + ", i64 " + indexValue);
    return elementPointer;
}

void LlvmEmitter::emitIndexLoad(const LirIndexInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const std::string elementPointer = emitIndexAddress(value.object, value.index, value.type, state, lines);
    const auto layout = abiLayoutForType(value.type);
    if (!layout.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support indexed element type '" + value.type + "'.");
    }
    const std::string result = localName(value.result);
    lines.push_back(
        "  " + result + " = load " + llvmType(value.type) + ", ptr " + elementPointer +
        ", align " + std::to_string(layout->align));
    state.valueTypes[value.result] = value.type;
    state.namedValues[value.result] = result;
}

void LlvmEmitter::emitStrRangeView(
    const LirStrRangeViewInst& value,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(value.receiver.type)));
    if (baseType != "Str") {
        throw std::runtime_error("LLVM Str range-view lowering received non-Str receiver type '" +
            value.receiver.type + "'.");
    }

    const std::string receiverValue = ensureValue(value.receiver, state, lines);
    const std::string basePointer = extractPointer(receiverValue, value.receiver.type, state, lines);
    const std::string lengthValue = extractLength(receiverValue, value.receiver.type, state, lines);
    const std::string rangeValue = ensureValue(value.range, state, lines);
    const std::string rangeLlvmType = llvmType(value.range.type);
    const std::string start = state.temp("str.range.start");
    const std::string end = state.temp("str.range.end");
    const std::string inclusive = state.temp("str.range.inclusive");
    lines.push_back("  " + start + " = extractvalue " + rangeLlvmType + " " + rangeValue + ", 0");
    lines.push_back("  " + end + " = extractvalue " + rangeLlvmType + " " + rangeValue + ", 1");
    lines.push_back("  " + inclusive + " = extractvalue " + rangeLlvmType + " " + rangeValue + ", 2");

    const std::string startNonnegative = state.temp("str.range.start.nonnegative");
    const std::string endNonnegative = state.temp("str.range.end.nonnegative");
    const std::string ordered = state.temp("str.range.ordered");
    const std::string endAsI64 = state.temp("str.range.end.i64");
    const std::string endPlusOne = state.temp("str.range.end.plus_one");
    const std::string effectiveEnd = state.temp("str.range.end.exclusive");
    const std::string startAsI64 = state.temp("str.range.start.i64");
    const std::string startWithin = state.temp("str.range.start.within");
    const std::string endWithin = state.temp("str.range.end.within");
    lines.push_back("  " + startNonnegative + " = icmp sge i32 " + start + ", 0");
    lines.push_back("  " + endNonnegative + " = icmp sge i32 " + end + ", 0");
    lines.push_back("  " + ordered + " = icmp sle i32 " + start + ", " + end);
    lines.push_back("  " + startAsI64 + " = sext i32 " + start + " to i64");
    lines.push_back("  " + endAsI64 + " = sext i32 " + end + " to i64");
    lines.push_back("  " + endPlusOne + " = add i64 " + endAsI64 + ", 1");
    lines.push_back("  " + effectiveEnd + " = select i1 " + inclusive + ", i64 " + endPlusOne + ", i64 " + endAsI64);
    lines.push_back("  " + startWithin + " = icmp ule i64 " + startAsI64 + ", " + lengthValue);
    lines.push_back("  " + endWithin + " = icmp ule i64 " + effectiveEnd + ", " + lengthValue);

    const std::string nonnegative = state.temp("str.range.nonnegative");
    const std::string endpointsSafe = state.temp("str.range.endpoints.safe");
    const std::string validRange = state.temp("str.range.valid");
    lines.push_back("  " + nonnegative + " = and i1 " + startNonnegative + ", " + endNonnegative);
    lines.push_back("  " + endpointsSafe + " = and i1 " + startWithin + ", " + endWithin);
    const std::string rangeChecks = state.temp("str.range.checks");
    lines.push_back("  " + rangeChecks + " = and i1 " + nonnegative + ", " + ordered);
    lines.push_back("  " + validRange + " = and i1 " + rangeChecks + ", " + endpointsSafe);

    const std::string rangeValidLabel = state.inlineLabel("str.range.bounds_ok");
    const std::string invalidLabel = state.inlineLabel("str.range.invalid");
    const std::string endCheckLabel = state.inlineLabel("str.range.check_end_utf8");
    const std::string successLabel = state.inlineLabel("str.range.success");
    const std::string joinLabel = state.inlineLabel("str.range.join");
    lines.push_back("  br i1 " + validRange + ", label %" + rangeValidLabel + ", label %" + invalidLabel);
    lines.push_back(rangeValidLabel + ":");

    auto emitBoundaryCheck = [&](const std::string& offset, const std::string& nextLabel, std::string_view suffix) {
        const std::string atZero = state.temp(std::string("str.range.") + std::string(suffix) + ".at_zero");
        const std::string atEnd = state.temp(std::string("str.range.") + std::string(suffix) + ".at_end");
        const std::string atNaturalBoundary = state.temp(std::string("str.range.") + std::string(suffix) + ".natural_boundary");
        const std::string inspectLabel = state.inlineLabel(std::string("str.range.") + std::string(suffix) + ".inspect");
        const std::string byteLabel = state.inlineLabel(std::string("str.range.") + std::string(suffix) + ".byte");
        const std::string inString = state.temp(std::string("str.range.") + std::string(suffix) + ".in_string");
        lines.push_back("  " + atZero + " = icmp eq i64 " + offset + ", 0");
        lines.push_back("  " + atEnd + " = icmp eq i64 " + offset + ", " + lengthValue);
        lines.push_back("  " + atNaturalBoundary + " = or i1 " + atZero + ", " + atEnd);
        lines.push_back("  br i1 " + atNaturalBoundary + ", label %" + nextLabel + ", label %" + inspectLabel);
        lines.push_back(inspectLabel + ":");
        lines.push_back("  " + inString + " = icmp ult i64 " + offset + ", " + lengthValue);
        lines.push_back("  br i1 " + inString + ", label %" + byteLabel + ", label %" + invalidLabel);
        lines.push_back(byteLabel + ":");
        const std::string bytePointer = state.temp(std::string("str.range.") + std::string(suffix) + ".byte_ptr");
        const std::string byteValue = state.temp(std::string("str.range.") + std::string(suffix) + ".byte");
        const std::string byteClass = state.temp(std::string("str.range.") + std::string(suffix) + ".byte_class");
        const std::string isBoundary = state.temp(std::string("str.range.") + std::string(suffix) + ".is_boundary");
        lines.push_back("  " + bytePointer + " = getelementptr inbounds i8, ptr " + basePointer + ", i64 " + offset);
        lines.push_back("  " + byteValue + " = load i8, ptr " + bytePointer + ", align 1");
        lines.push_back("  " + byteClass + " = and i8 " + byteValue + ", 192");
        lines.push_back("  " + isBoundary + " = icmp ne i8 " + byteClass + ", 128");
        lines.push_back("  br i1 " + isBoundary + ", label %" + nextLabel + ", label %" + invalidLabel);
    };

    emitBoundaryCheck(startAsI64, endCheckLabel, "start");
    lines.push_back(endCheckLabel + ":");
    emitBoundaryCheck(effectiveEnd, successLabel, "end");
    lines.push_back(successLabel + ":");

    const std::string resultPointer = state.temp("str.range.result_ptr");
    const std::string resultLength = state.temp("str.range.result_len");
    const std::string sliceWithPointer = state.temp("str.range.slice_ptr");
    const std::string sliceValue = state.temp("str.range.slice");
    lines.push_back("  " + resultPointer + " = getelementptr inbounds i8, ptr " + basePointer + ", i64 " + startAsI64);
    lines.push_back("  " + resultLength + " = sub i64 " + effectiveEnd + ", " + startAsI64);
    lines.push_back("  " + sliceWithPointer + " = insertvalue %claw.slice poison, ptr " + resultPointer + ", 0");
    lines.push_back("  " + sliceValue + " = insertvalue %claw.slice " + sliceWithPointer + ", i64 " + resultLength + ", 1");

    if (value.safeResult) {
        const auto addressIt = state.namedAddresses.find(value.result);
        if (addressIt == state.namedAddresses.end()) {
            throw std::runtime_error("LLVM lowering did not preallocate safe Str range result '" + value.result + "'.");
        }
        const LirValue borrowedSlice{sliceValue, "ref Str", false};
        constructChoiceValueAtAddress(addressIt->second, value.type, "Some", {borrowedSlice}, state, lines);
        lines.push_back("  br label %" + joinLabel);
        lines.push_back(invalidLabel + ":");
        constructChoiceValueAtAddress(addressIt->second, value.type, "None", {}, state, lines);
        lines.push_back("  br label %" + joinLabel);
        lines.push_back(joinLabel + ":");
        state.valueTypes[value.result] = value.type;
    } else {
        lines.push_back("  br label %" + joinLabel);
        lines.push_back(invalidLabel + ":");
        lines.push_back("  call void @llvm.trap()");
        lines.push_back("  unreachable");
        lines.push_back(joinLabel + ":");
        state.valueTypes[value.result] = value.type;
        state.namedValues[value.result] = sliceValue;
        needsTrap = true;
    }
}

void LlvmEmitter::emitIndexStore(const LirStoreIndexInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(value.object.type)));
    if (baseType == "Str") {
        throw std::runtime_error("LLVM lowering does not permit indexed stores into Str; string indexing is read-only.");
    }
    const std::string elementPointer = emitIndexAddress(value.object, value.index, value.value.type, state, lines);
    const auto layout = abiLayoutForType(value.value.type);
    if (!layout.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support indexed element type '" + value.value.type + "'.");
    }
    const std::string operand = ensureValue(value.value, state, lines);
    storeValueToAddress(elementPointer, value.value.type, operand, layout->align, lines);
}

std::string LlvmEmitter::ensureValue(const LirValue& value, FunctionState& state, std::vector<std::string>& lines) {
    const std::string llvmValueType = llvmType(value.type);
    if (llvmValueType == "void" || value.isUnit) {
        return {};
    }

    if (isStringLiteral(value.text)) {
        const auto& stringConst = internString(value.text);
        const std::string ptrReg = state.temp("str.ptr");
        lines.push_back("  " + ptrReg + " = getelementptr inbounds " + stringConst.arrayType + ", ptr " + stringConst.globalName + ", i64 0, i64 0");
        const std::string sliceBase = state.temp("str.slice");
        lines.push_back("  " + sliceBase + " = insertvalue %claw.slice poison, ptr " + ptrReg + ", 0");
        const std::string sliceValue = state.temp("str.slice");
        lines.push_back("  " + sliceValue + " = insertvalue %claw.slice " + sliceBase + ", i64 " + std::to_string(stringConst.length) + ", 1");
        return sliceValue;
    }

    const auto layout = abiLayoutForType(value.type);
    const size_t align = layout.has_value() ? layout->align : 0;
    const std::string& valueKey = value.bindingKey.empty() ? value.text : value.bindingKey;

    if (const auto it = state.stackPointers.find(valueKey); it != state.stackPointers.end()) {
        return loadValueFromAddress(it->second, value.type, align, state, lines);
    }

    if (const auto it = state.namedValues.find(valueKey); it != state.namedValues.end()) {
        return it->second;
    }

    if (const auto it = state.namedAddresses.find(valueKey); it != state.namedAddresses.end()) {
        return loadValueFromAddress(it->second, value.type, align, state, lines);
    }

    if (const auto statIt = staticsByName.find(value.text); statIt != staticsByName.end()) {
        return loadValueFromAddress(quoteGlobal(statIt->second->link.symbol), value.type, align, state, lines);
    }

    if (const auto it = state.params.find(valueKey); it != state.params.end()) {
        return it->second;
    }

    if (!value.text.empty() && value.text.front() == '%') {
        return localName(value.text);
    }
    if (value.text == "true") {
        return "true";
    }
    if (value.text == "false") {
        return "false";
    }
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(value.type)));
    if (baseType == "Float32" || baseType == "Float64") {
        return llvmFloatConstant(value.text, baseType);
    }
    if (isIntegerLiteral(value.text)) {
        std::string_view magnitudeText = value.text;
        bool isNegative = false;
        if (!magnitudeText.empty() && magnitudeText.front() == '-') {
            isNegative = true;
            magnitudeText.remove_prefix(1);
        }
        const auto literal = frontend::numeric_literals::parseIntegerLiteralText(std::string(magnitudeText));
        if (!literal.valid) {
            throw std::runtime_error("LLVM lowering could not parse integer literal '" + value.text + "'.");
        }
        return isNegative ? "-" + literal.magnitude : literal.magnitude;
    }

    throw std::runtime_error("LLVM lowering does not yet support value '" + value.text + "' of type '" + value.type + "'.");
}

} // namespace claw::codegen
