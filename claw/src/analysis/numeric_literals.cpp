#include "analysis/numeric_literals.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace claw::frontend::numeric_literals {
namespace {

ResolvedType makePlainType(const std::string& name) {
    ResolvedType type;
    type.name = name;
    type.category = TypeCategory::Plain;
    return type;
}

bool literalMagnitudeHasNonZeroDigit(const std::string& magnitude) {
    for (char c : magnitude) {
        if (std::isdigit(static_cast<unsigned char>(c)) && c != '0') {
            return true;
        }
    }
    return false;
}

using UInt128 = unsigned __int128;

bool tryParseUnsignedDecimal(const std::string& text, UInt128* value) {
    if (!value || text.empty()) {
        return false;
    }

    UInt128 parsed = 0;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        const unsigned digit = static_cast<unsigned>(c - '0');
        const UInt128 maxValue = std::numeric_limits<UInt128>::max();
        if (parsed > (maxValue - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }

    *value = parsed;
    return true;
}

UInt128 maxUnsignedBits(unsigned bits) {
    if (bits >= 128) {
        return std::numeric_limits<UInt128>::max();
    }
    return (UInt128{1} << bits) - 1;
}

UInt128 maxSignedPositiveBits(unsigned bits) {
    if (bits <= 1) {
        return 0;
    }
    return maxUnsignedBits(bits - 1);
}

std::optional<unsigned> integerLikeBitWidth(const std::string& typeName) {
    static const std::unordered_map<std::string, unsigned> widths = {
        {"Int8", 8}, {"Int16", 16}, {"Int32", 32}, {"Int64", 64}, {"Int128", 128},
        {"UInt8", 8}, {"UInt16", 16}, {"UInt32", 32}, {"UInt64", 64}, {"UInt128", 128}};
    const auto it = widths.find(typeName);
    return it != widths.end() ? std::optional<unsigned>(it->second) : std::nullopt;
}

} // namespace

bool isConcreteNumericType(const ResolvedType& type) {
    return type.isPlain() && type.viewKind.empty() && isNumericTypeName(type.name);
}

ResolvedType makeIntegerLiteralType() {
    ResolvedType type;
    type.name = "IntLiteral";
    type.category = TypeCategory::Plain;
    return type;
}

ResolvedType makeFloatLiteralType() {
    ResolvedType type;
    type.name = "FloatLiteral";
    type.category = TypeCategory::Plain;
    return type;
}

bool isFloatLiteralType(const ResolvedType& type) {
    return type.category == TypeCategory::Plain && type.viewKind.empty() && type.name == "FloatLiteral";
}

bool isNumericLiteralType(const ResolvedType& type) {
    return isIntegerLiteralType(type) || isFloatLiteralType(type);
}

bool isFloatTypeName(const std::string& name) {
    return name == "Float32" || name == "Float64";
}

NumericLiteralParts splitNumericLiteralText(const std::string& text) {
    const size_t suffixPos = text.find('_');
    if (suffixPos == std::string::npos) {
        return {text, {}};
    }
    return {text.substr(0, suffixPos), text.substr(suffixPos + 1)};
}

std::optional<ResolvedType> resolveNumericLiteralSuffixType(const std::string& suffix) {
    if (suffix.empty() || !isNumericTypeName(suffix)) {
        return std::nullopt;
    }
    return makePlainType(suffix);
}

bool integerLiteralFitsTarget(const std::string& magnitude, const std::string& targetName, const TargetSpec& target) {
    UInt128 value = 0;
    if (!tryParseUnsignedDecimal(magnitude, &value)) {
        return false;
    }

    const unsigned pointerWidthBits = std::min(target.pointerWidthBits == 0 ? 64u : target.pointerWidthBits, 128u);
    const unsigned ptrdiffWidthBits = std::min(target.ptrdiffWidthBits == 0 ? 64u : target.ptrdiffWidthBits, 128u);

    if (targetName == "Char") {
        return value <= UInt128{0x10FFFF};
    }
    if (targetName == "USize") {
        return value <= maxUnsignedBits(pointerWidthBits);
    }



    if (const auto bits = integerLikeBitWidth(targetName)) {
        if (targetName.rfind("Int", 0) == 0) {
            return value <= maxSignedPositiveBits(*bits);
        }
        return value <= maxUnsignedBits(*bits);
    }

    if (targetName == "Float32") {
        return value <= UInt128{1} << 24;
    }
    if (targetName == "Float64") {
        return value <= UInt128{1} << 53;
    }

    return false;
}

bool floatLiteralFitsTarget(const std::string& magnitude, const std::string& targetName) {
    if (!isFloatTypeName(targetName)) {
        return false;
    }

    try {
        const long double value = std::stold(magnitude);
        if (!std::isfinite(value)) {
            return false;
        }
        const bool nonZeroMagnitude = literalMagnitudeHasNonZeroDigit(magnitude);
        if (targetName == "Float32") {
            const float narrowed = static_cast<float>(value);
            return std::isfinite(narrowed) && !(narrowed == 0.0f && nonZeroMagnitude);
        }
        const double narrowed = static_cast<double>(value);
        return std::isfinite(narrowed) && !(narrowed == 0.0 && nonZeroMagnitude);
    } catch (const std::exception&) {
        return false;
    }
}

bool shouldUseExpectedIntegerType(const ResolvedType* expectedType) {
    return expectedType && expectedType->isPlain() && expectedType->viewKind.empty() &&
           isIntegerLikeTypeName(expectedType->name);
}

bool shouldUseExpectedFloatType(const ResolvedType* expectedType) {
    return expectedType && expectedType->isPlain() && expectedType->viewKind.empty() &&
           isFloatTypeName(expectedType->name);
}

ResolvedType defaultIntegerLiteralType() {
    return makePlainType("Int32");
}

ResolvedType defaultFloatLiteralType() {
    return makePlainType("Float64");
}

ResolvedType normalizeInferredLiteralType(const ResolvedType& type) {
    if (isIntegerLiteralType(type)) {
        return defaultIntegerLiteralType();
    }
    if (isFloatLiteralType(type)) {
        return defaultFloatLiteralType();
    }
    return type;
}

std::optional<ResolvedType> numericLiteralContextType(
    const ResolvedType* expectedType,
    const ResolvedType& leftType,
    const ResolvedType& rightType) {
    if (expectedType && isConcreteNumericType(*expectedType)) {
        return *expectedType;
    }
    if (isConcreteNumericType(leftType)) {
        return leftType;
    }
    if (isConcreteNumericType(rightType)) {
        return rightType;
    }
    if (isFloatLiteralType(leftType) || isFloatLiteralType(rightType)) {
        return defaultFloatLiteralType();
    }
    if (isIntegerLiteralType(leftType) && isIntegerLiteralType(rightType)) {
        return defaultIntegerLiteralType();
    }
    return std::nullopt;
}

} // namespace claw::frontend::numeric_literals
