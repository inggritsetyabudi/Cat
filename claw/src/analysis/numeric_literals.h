#pragma once

#include "analysis/types.h"

#include <cstdint>
#include <optional>
#include <string>

namespace claw::frontend::numeric_literals {

struct NumericLiteralParts {
    std::string magnitude;
    std::string suffix;
};

struct ParsedIntegerLiteral {
    bool valid = false;
    std::string magnitude;
    std::string suffix;
    unsigned radix = 10;
};

struct ParsedFloatLiteral {
    // Native target IEEE bits; Float32 occupies the low 32 bits, not LLVM's
    // textual encoding of an exactly promoted double-precision value.
    std::uint64_t bits = 0;
    unsigned bitWidth = 0;
};

ResolvedType makeIntegerLiteralType();
ResolvedType makeFloatLiteralType();
bool isFloatLiteralType(const ResolvedType& type);
bool isNumericLiteralType(const ResolvedType& type);
bool isConcreteNumericType(const ResolvedType& type);
bool isFloatTypeName(const std::string& name);
NumericLiteralParts splitNumericLiteralText(const std::string& text);
ParsedIntegerLiteral parseIntegerLiteralText(const std::string& text);
bool isValidFloatLiteralText(const std::string& text);
// Converts normalized decimal magnitude (no suffix/separators) directly to the
// target. Finite subnormals and exact zero are valid; nonzero rounded to zero is not.
std::optional<ParsedFloatLiteral> parseFloatLiteralTarget(
    const std::string& magnitude,
    const std::string& targetName);
std::optional<ResolvedType> resolveNumericLiteralSuffixType(const std::string& suffix);
bool integerLiteralFitsTarget(const std::string& magnitude, const std::string& targetName, const TargetSpec& target);
bool integerLiteralFitsNegatedTarget(const std::string& magnitude, const std::string& targetName, const TargetSpec& target);
bool floatLiteralFitsTarget(const std::string& magnitude, const std::string& targetName);
bool shouldUseExpectedIntegerType(const ResolvedType* expectedType);
bool shouldUseExpectedFloatType(const ResolvedType* expectedType);
ResolvedType defaultIntegerLiteralType();
ResolvedType defaultFloatLiteralType();
ResolvedType normalizeInferredLiteralType(const ResolvedType& type);
std::optional<ResolvedType> numericLiteralContextType(
    const ResolvedType* expectedType,
    const ResolvedType& leftType,
    const ResolvedType& rightType);

} // namespace claw::frontend::numeric_literals
