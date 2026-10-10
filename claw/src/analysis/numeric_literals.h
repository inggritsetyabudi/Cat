#pragma once

#include "analysis/types.h"

#include <optional>
#include <string>

namespace claw::frontend::numeric_literals {

struct NumericLiteralParts {
    std::string magnitude;
    std::string suffix;
};

ResolvedType makeIntegerLiteralType();
ResolvedType makeFloatLiteralType();
bool isFloatLiteralType(const ResolvedType& type);
bool isNumericLiteralType(const ResolvedType& type);
bool isConcreteNumericType(const ResolvedType& type);
bool isFloatTypeName(const std::string& name);
NumericLiteralParts splitNumericLiteralText(const std::string& text);
std::optional<ResolvedType> resolveNumericLiteralSuffixType(const std::string& suffix);
bool integerLiteralFitsTarget(const std::string& magnitude, const std::string& targetName, const TargetSpec& target);
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
