#pragma once
#include "analysis/numeric_literals.h"
#include "codegen/llvm.h"
#include "lexer/literal_escapes.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace claw::codegen {

using namespace claw::frontend;

// One target-directed materialization path for local and static float literals.
std::string llvmFloatConstant(std::string_view rawText, std::string_view typeName);

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

inline std::string trim(std::string_view text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

inline std::string stripViewPrefix(std::string_view type) {
    const std::string trimmed = trim(type);
    if (trimmed.rfind("ref mut[", 0) == 0) {
        const size_t close = trimmed.find(']');
        if (close != std::string::npos) {
            return trim(std::string_view(trimmed).substr(close + 1));
        }
    }
    if (trimmed.rfind("ref[", 0) == 0) {
        const size_t close = trimmed.find(']');
        if (close != std::string::npos) {
            return trim(std::string_view(trimmed).substr(close + 1));
        }
    }
    if (trimmed.rfind("ref mut ", 0) == 0) {
        return trim(std::string_view(trimmed).substr(8));
    }
    if (trimmed.rfind("ref ", 0) == 0) {
        return trim(std::string_view(trimmed).substr(4));
    }
    if (trimmed.rfind("look ", 0) == 0 || trimmed.rfind("edit ", 0) == 0) {
        return trim(std::string_view(trimmed).substr(5));
    }
    return trimmed;
}

inline std::string stripGenericArgs(std::string_view type) {
    const std::string trimmed = trim(type);
    const size_t bracketPos = trimmed.find('[');
    if (bracketPos != std::string_view::npos) {
        return trim(std::string_view(trimmed).substr(0, bracketPos));
    }
    const size_t ofPos = trimmed.find(" of ");
    return ofPos == std::string_view::npos ? trimmed : trim(std::string_view(trimmed).substr(0, ofPos));
}

inline std::string canonicalBackendTypeBase(std::string_view base) {
    return std::string(trim(base));
}

inline std::string lowercaseBackendIdentifier(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char c : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lowered;
}

inline bool sameBackendIdentifier(std::string_view left, std::string_view right) {
    return lowercaseBackendIdentifier(left) == lowercaseBackendIdentifier(right);
}

inline bool isStringLiteral(std::string_view text) {
    return text.size() >= 2 && text.front() == '"' && text.back() == '"';
}

inline bool isIntegerLiteral(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    size_t index = 0;
    if (text.front() == '-') {
        if (text.size() == 1) {
            return false;
        }
        index = 1;
    }
    if (index >= text.size() || std::isdigit(static_cast<unsigned char>(text[index])) == 0) {
        return false;
    }
    for (; index < text.size(); ++index) {
        const char c = text[index];
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
            continue;
        }
        return false;
    }
    return true;
}

inline std::string stripLiteralSuffix(std::string_view text) {
    const size_t suffixPos = text.find('_');
    return std::string(suffixPos == std::string_view::npos ? text : text.substr(0, suffixPos));
}

inline std::string decodeStringLiteral(std::string_view text) {
    if (!isStringLiteral(text)) {
        return std::string(text);
    }
    const auto decoded = literal_escapes::decodeStringContent(text.substr(1, text.size() - 2));
    if (!decoded.valid()) {
        throw std::runtime_error(literal_escapes::errorMessage(
            decoded.error, literal_escapes::LiteralKind::String));
    }
    return decoded.value;
}

inline std::string escapeLlvmBytes(const std::string& text) {
    std::ostringstream out;
    for (const unsigned char c : text) {
        if (c == '\\') {
            out << "\\5C";
        } else if (c == '"') {
            out << "\\22";
        } else if (c >= 32 && c <= 126) {
            out << static_cast<char>(c);
        } else {
            out << '\\' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(c) << std::nouppercase << std::dec;
        }
    }
    return out.str();
}

inline std::string quoteGlobal(std::string_view name) {
    return std::string("@\"") + std::string(name) + "\"";
}

inline std::string quoteType(std::string_view name) {
    return std::string("%\"") + std::string(name) + "\"";
}

inline std::string localName(std::string_view name) {
    std::string out;
    out.reserve(name.size() + 1);
    out.push_back('%');
    for (const char c : name) {
        if (c == '%') {
            continue;
        }
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.') ? c : '_');
    }
    return out;
}

inline std::string blockLabel(std::string_view label) {
    std::string out;
    out.reserve(label.size());
    for (const char c : label) {
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == '-') ? c : '_');
    }
    return out.empty() ? std::string("block") : out;
}

inline std::string tailSegment(std::string_view text) {
    const size_t pos = text.rfind('.');
    return pos == std::string_view::npos ? std::string(text) : std::string(text.substr(pos + 1));
}

inline std::string receiverSegment(std::string_view text) {
    const size_t pos = text.rfind('.');
    return pos == std::string_view::npos ? std::string() : std::string(text.substr(0, pos));
}

inline bool isSignedIntegerType(std::string_view typeName) {
    return typeName == "Int8" || typeName == "Int16" || typeName == "Int32" || typeName == "Int64" ||
        typeName == "Int128";
}

inline bool isUnsignedIntegerType(std::string_view typeName) {
    return typeName == "UInt8" || typeName == "UInt16" || typeName == "UInt32" || typeName == "UInt64" ||
        typeName == "UInt128" || typeName == "USize";
}

inline bool isFloatingType(std::string_view typeName) {
    return typeName == "Float32" || typeName == "Float64";
}

struct ParsedTypeName {
    std::string base;
    std::vector<std::string> args;
};

inline size_t roundUpTo(size_t value, size_t align) {
    return align == 0 ? value : ((value + align - 1) / align) * align;
}

inline std::vector<std::string> splitTypeArgs(std::string_view text) {
    std::vector<std::string> args;
    size_t bracketDepth = 0;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '[') {
            ++bracketDepth;
            continue;
        }
        if (c == ']') {
            bracketDepth = bracketDepth == 0 ? 0 : bracketDepth - 1;
            continue;
        }
        if (c == ',' && bracketDepth == 0) {
            args.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    if (start <= text.size()) {
        args.push_back(trim(text.substr(start)));
    }
    return args;
}

inline ParsedTypeName parseTypeName(std::string_view typeText) {
    const std::string stripped = trim(stripViewPrefix(typeText));
    const size_t bracketPos = stripped.find('[');
    if (bracketPos != std::string::npos && stripped.back() == ']') {
        return ParsedTypeName{
            canonicalBackendTypeBase(stripped.substr(0, bracketPos)),
            splitTypeArgs(trim(stripped.substr(bracketPos + 1, stripped.size() - bracketPos - 2)))};
    }
    const size_t ofPos = stripped.find(" of ");
    if (ofPos == std::string::npos) {
        return ParsedTypeName{canonicalBackendTypeBase(stripped), {}};
    }
    return ParsedTypeName{
        canonicalBackendTypeBase(trim(stripped.substr(0, ofPos))),
        splitTypeArgs(trim(stripped.substr(ofPos + 4)))};
}

inline std::optional<std::string> fixedArrayElementTypeFromType(std::string_view typeText) {
    const ParsedTypeName parsed = parseTypeName(typeText);
    if (parsed.base != "Array" || parsed.args.size() != 2 ||
        (parsed.args.front() != "Int32" && parsed.args.front() != "UInt8")) {
        return std::nullopt;
    }
    return parsed.args.front();
}

inline size_t fixedArrayElementSizeFromType(std::string_view typeText) {
    const auto elementType = fixedArrayElementTypeFromType(typeText);
    if (!elementType.has_value()) {
        return 0;
    }
    return *elementType == "UInt8" ? 1 : 4;
}

inline std::optional<size_t> fixedArrayLengthFromType(std::string_view typeText) {
    const ParsedTypeName parsed = parseTypeName(typeText);
    const size_t elementSize = fixedArrayElementSizeFromType(typeText);
    if (!elementSize) {
        return std::nullopt;
    }
    const auto literal = numeric_literals::parseIntegerLiteralText(parsed.args[1]);
    if (!literal.valid || !literal.suffix.empty()) {
        return std::nullopt;
    }
    unsigned long long value = 0;
    const auto converted = std::from_chars(
        literal.magnitude.data(), literal.magnitude.data() + literal.magnitude.size(), value);
    if (converted.ec != std::errc{} || converted.ptr != literal.magnitude.data() + literal.magnitude.size() || value == 0 ||
        value > std::numeric_limits<size_t>::max()) {
        return std::nullopt;
    }
    const size_t length = static_cast<size_t>(value);
    if (!fixedArrayByteSize(length, elementSize, defaultTargetSpec()).has_value()) {
        return std::nullopt;
    }
    return length;
}

inline std::string joinTypeArgs(const std::vector<std::string>& args) {
    std::string joined;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += args[i];
    }
    return joined;
}

inline std::string substituteTypeText(
    std::string_view typeText,
    const std::unordered_map<std::string, std::string>& bindings) {
    const std::string trimmed = trim(typeText);
    std::string prefix;
    if (trimmed.rfind("ref mut[", 0) == 0) {
        const size_t close = trimmed.find(']');
        prefix = close != std::string::npos ? trimmed.substr(0, close + 1) + " " : std::string("ref mut ");
    } else if (trimmed.rfind("ref[", 0) == 0) {
        const size_t close = trimmed.find(']');
        prefix = close != std::string::npos ? trimmed.substr(0, close + 1) + " " : std::string("ref ");
    } else {
        prefix =
            trimmed.rfind("ref mut ", 0) == 0 ? std::string("ref mut ") :
            (trimmed.rfind("ref ", 0) == 0 ? std::string("ref ") :
            (trimmed.rfind("look ", 0) == 0 ? std::string("look ") :
            (trimmed.rfind("edit ", 0) == 0 ? std::string("edit ") : std::string{})));
    }
    const ParsedTypeName parsed = parseTypeName(typeText);
    if (parsed.args.empty()) {
        const auto it = bindings.find(parsed.base);
        return prefix + (it != bindings.end() ? it->second : parsed.base);
    }

    std::vector<std::string> substitutedArgs;
    substitutedArgs.reserve(parsed.args.size());
    for (const auto& arg : parsed.args) {
        substitutedArgs.push_back(substituteTypeText(arg, bindings));
    }
    return prefix + parsed.base + "[" + joinTypeArgs(substitutedArgs) + "]";
}

inline std::string choiceStorageType(size_t tagSizeBytes, size_t payloadOffsetBytes, size_t payloadSizeBytes) {
    std::ostringstream out;
    out << "{ i" << (tagSizeBytes * 8);
    const size_t paddingBytes = payloadOffsetBytes > tagSizeBytes ? payloadOffsetBytes - tagSizeBytes : 0;
    if (paddingBytes > 0) {
        out << ", [" << paddingBytes << " x i8]";
    }
    if (payloadSizeBytes > 0) {
        out << ", [" << payloadSizeBytes << " x i8]";
    }
    out << " }";
    return out.str();
}

struct StringConstantInfo {
    std::string globalName;
    std::string arrayType;
    size_t length = 0;
};

struct PendingChoiceBinding {
    LirValue source;
    std::string variantName;
    std::vector<std::string> bindingNames;
    std::vector<std::string> bindingKeys;
};

struct PendingIterBinding {
    LirValue iterable;
    std::string itemName;
    std::string indexName;
    std::string itemType;
    std::string stateKey;
    std::string itemBindingKey;
    std::string indexBindingKey;
};

struct ConcreteChoiceCaseInfo {
    std::string name;
    std::vector<std::string> payloadTypes;
    LayoutVariantInfo layout;
};

struct ConcreteChoiceInfo {
    std::string typeText;
    std::string llvmTypeText;
    size_t tagSizeBytes = 4;
    size_t payloadOffsetBytes = 0;
    size_t payloadSizeBytes = 0;
    size_t sizeBytes = 0;
    size_t alignBytes = 1;
    std::vector<ConcreteChoiceCaseInfo> cases;
};

struct AbiLayout {
    size_t size = 0;
    size_t align = 1;
};

struct IteratorState {
    std::string indexSlot;
    std::string itemSlot;
    std::string rangeCurrentSlot;
    std::string rangeEndSlot;
    std::string rangeInclusiveSlot;
    std::string rangeFinishedSlot;
    std::string visibleIndexSlot;
    size_t itemAlign = 1;
    bool isRange = false;
};

struct FunctionState {
    const LirFunction& function;
    std::unordered_map<std::string, std::string> stackPointers;
    std::unordered_set<std::string> uninitializedOwnedAggregates;
    std::unordered_map<std::string, std::unordered_set<std::string>> initializedAggregateFields;
    std::unordered_map<std::string, std::string> params;
    std::unordered_map<std::string, std::string> namedValues;
    std::unordered_map<std::string, std::string> namedAddresses;
    std::unordered_map<std::string, std::string> valueTypes;
    std::unordered_set<std::string> allocatedLocalNames;
    std::optional<std::string> returnSlot;
    std::unordered_map<std::string, std::vector<PendingChoiceBinding>> pendingChoiceBindings;
    std::unordered_map<std::string, std::vector<PendingIterBinding>> pendingIterBindings;
    std::unordered_map<std::string, IteratorState> iteratorStates;
    // Function-entry storage plus constant iterator-state initialization only.
    // Never put source initializers, calls, drops or body-SSA-dependent stores
    // here: those must execute at their original control-flow location.
    std::vector<std::string> entryAllocas;
    int nextLocal = 0;
    int nextInlineBlock = 0;

    std::string temp(std::string_view prefix) {
        return localName(std::string(prefix) + "." + std::to_string(nextLocal++));
    }

    std::string inlineLabel(std::string_view prefix) {
        return blockLabel(function.name + "." + std::string(prefix) + "." + std::to_string(nextInlineBlock++));
    }
};

class LlvmEmitter {
public:
    explicit LlvmEmitter(const LirProgram& program, bool emitNativeEntry = false)
        : program(program),
          emitNativeEntry(emitNativeEntry) {
        collectDecls();
    }

    std::string emit();

private:
    const LirProgram& program;
    bool emitNativeEntry = false;
    std::unordered_map<std::string, const LirFunction*> functionsByName;
    std::unordered_map<std::string, const LirShape*> shapesByName;
    std::unordered_map<std::string, const LirChoice*> choicesByName;
    std::unordered_map<std::string, const LirStatic*> staticsByName;
    std::unordered_map<std::string, std::unordered_map<std::string, size_t>> shapeFieldIndices;
    mutable std::unordered_map<std::string, TypeLayoutInfo> concreteShapeLayouts;
    mutable std::unordered_set<std::string> concreteShapeLayoutsInProgress;
    std::string currentEntrySymbol;
    std::unordered_map<std::string, StringConstantInfo> stringPool;
    std::vector<std::string> runtimeDecls;
    std::unordered_set<std::string> runtimeDeclKeys;
    std::vector<std::string> externalDecls;
    std::unordered_set<std::string> externalDeclKeys;
    bool needsTrap = false;

    void collectDecls();
    const LirFunction* entryFunction() const;
    std::string llvmType(const std::string& typeText) const;
    std::string emitTypeDecls() const;
    const StringConstantInfo& internString(std::string_view literal);
    std::string emitStringGlobals();
    std::string emitStaticGlobals();
    void addRuntimeDecl(const std::string& decl);
    void addExternalDecl(const std::string& decl);
    bool usesRuntimePointerAbi(const std::string& argType) const;
    std::string llvmRuntimeParamType(const std::string& argType) const;
    std::string runtimeSymbol(std::string_view callee, const std::string& argType);
    std::string externalSymbol(const LirCallInst& call);
    std::string emitDeclarations();
    std::optional<AbiLayout> abiLayoutForType(std::string_view typeText) const;
    const TypeLayoutInfo* resolveShapeLayout(std::string_view typeText) const;
    std::optional<ConcreteChoiceInfo> resolveChoiceType(std::string_view typeText) const;
    bool usesIndirectAbi(std::string_view typeText, AbiPassKind passKind) const;
    bool usesBorrowPointerAbi(std::string_view typeText, AbiPassKind passKind) const;
    bool usesPointerAbi(std::string_view typeText, AbiPassKind passKind) const;
    std::string llvmParamAbiType(const LirParam& param) const;
    std::string llvmFunctionReturnType(const LirFunction& fn) const;
    const LirFunction* lookupDirectFunction(std::string_view callee) const;
    void materializeChoiceBinding(
        const PendingChoiceBinding& binding,
        FunctionState& state,
        std::vector<std::string>& lines);
    IteratorState& ensureIteratorState(
        std::string_view stateKey,
        std::string_view itemName,
        std::string_view indexName,
        std::string_view itemType,
        FunctionState& state);
    IteratorState& ensureRangeIteratorState(
        std::string_view stateKey,
        std::string_view itemName,
        std::string_view indexName,
        std::string_view itemType,
        FunctionState& state);
    void materializeIterBinding(
        const PendingIterBinding& binding,
        FunctionState& state,
        std::vector<std::string>& lines);
    void materializePendingBlockBindings(
        std::string_view blockLabel,
        FunctionState& state,
        std::vector<std::string>& lines);
    std::string valueTypeForName(std::string_view name, const FunctionState& state) const;
    std::string ensureNamedValue(std::string_view name, FunctionState& state, std::vector<std::string>& lines);
    std::optional<std::string> tryAddressOfValue(const LirValue& value, FunctionState& state, std::vector<std::string>& lines);
    std::string ensureAddress(const LirValue& value, FunctionState& state, std::vector<std::string>& lines);
    bool typeHasOwnedDrop(std::string_view typeText) const;
    void emitOwnedDropAtAddress(
        std::string_view typeText,
        std::string_view address,
        FunctionState& state,
        std::vector<std::string>& lines);
    const LayoutFieldInfo* findShapeFieldLayout(std::string_view typeText, std::string_view field) const;
    size_t findShapeFieldIndex(std::string_view typeText, std::string_view field) const;
    std::string loadValueFromAddress(
        std::string_view address,
        const std::string& typeText,
        size_t align,
        FunctionState& state,
        std::vector<std::string>& lines) const;
    void storeValueToAddress(
        std::string_view address,
        const std::string& typeText,
        const std::string& operand,
        size_t align,
        std::vector<std::string>& lines) const;
    std::string emitFieldAddress(
        const LirValue& object,
        std::string_view field,
        FunctionState& state,
        std::vector<std::string>& lines);
    std::string extractLength(
        const std::string& aggregateValue,
        std::string_view receiverType,
        FunctionState& state,
        std::vector<std::string>& lines);
    std::string extractPointer(
        const std::string& aggregateValue,
        std::string_view receiverType,
        FunctionState& state,
        std::vector<std::string>& lines);
    void emitIterNext(
        const LirIterNextInst& value,
        std::string_view currentBlockLabel,
        FunctionState& state,
        std::vector<std::string>& lines);
    void emitRangeScanInit(
        const LirRangeScanInitInst& value,
        FunctionState& state,
        std::vector<std::string>& lines);
    void constructChoiceValueAtAddress(
        std::string_view address,
        const std::string& typeText,
        std::string_view variantName,
        const std::vector<LirValue>& payloads,
        FunctionState& state,
        std::vector<std::string>& lines);
    void emitBoundsCheck(const LirBoundsCheckInst& value, FunctionState& state, std::vector<std::string>& lines);
    void emitSwitch(const LirSwitchInst& value, FunctionState& state, std::vector<std::string>& lines);
    bool emitTry(const LirTryInst& value, FunctionState& state, std::vector<std::string>& lines);
    void emitChoiceMakeInst(const LirChoiceMakeInst& value, FunctionState& state, std::vector<std::string>& lines);
    void emitBuiltinCall(const LirCallInst& value, FunctionState& state, std::vector<std::string>& lines);
    void emitCallInst(const LirCallInst& value, FunctionState& state, std::vector<std::string>& lines);
    void emitStoreField(const LirStoreFieldInst& value, FunctionState& state, std::vector<std::string>& lines);
    std::string emitIndexAddress(
        const LirValue& object,
        const LirValue& index,
        const std::string& elementType,
        FunctionState& state,
        std::vector<std::string>& lines);
    void emitIndexLoad(const LirIndexInst& value, FunctionState& state, std::vector<std::string>& lines);
    void emitStrRangeView(const LirStrRangeViewInst& value, FunctionState& state, std::vector<std::string>& lines);
    void emitIndexStore(const LirStoreIndexInst& value, FunctionState& state, std::vector<std::string>& lines);
    std::string ensureValue(const LirValue& value, FunctionState& state, std::vector<std::string>& lines);
    std::string symbolForDirectCall(std::string_view callee) const;
    std::string emitFunction(const LirFunction& fn);
    std::string emitFunctionDefs();
    std::string emitNativeEntryWrapper() const;
};

std::string llvmFunctionLinkage(const SymbolLinkInfo& linkage);

} // namespace claw::codegen
