#include "analysis/numeric_literals.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

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
        if (c == 'e' || c == 'E') {
            break;
        }
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

std::optional<size_t> exactNumericSuffixPosition(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '_') {
            continue;
        }
        const std::string suffix = text.substr(i + 1);
        if (!suffix.empty() && isNumericTypeName(suffix)) {
            return i;
        }
    }
    return std::nullopt;
}

std::string withoutSeparators(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        if (c != '_') {
            result += c;
        }
    }
    return result;
}

int digitValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool consumeDecimalDigits(std::string_view text, size_t& position) {
    bool sawDigit = false;
    bool previousWasDigit = false;
    while (position < text.size()) {
        const char c = text[position];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            sawDigit = true;
            previousWasDigit = true;
            ++position;
            continue;
        }
        if (c == '_') {
            if (!previousWasDigit || position + 1 >= text.size() ||
                !std::isdigit(static_cast<unsigned char>(text[position + 1]))) {
                return false;
            }
            previousWasDigit = false;
            ++position;
            continue;
        }
        break;
    }
    return sawDigit && previousWasDigit;
}

std::string toDecimalMagnitude(const std::string& digits, unsigned radix) {
    std::vector<unsigned> decimalDigits{0}; // little-endian decimal digits
    for (char c : digits) {
        if (c == '_') {
            continue;
        }
        const unsigned value = static_cast<unsigned>(digitValue(c));
        unsigned carry = value;
        for (unsigned& decimalDigit : decimalDigits) {
            const unsigned expanded = decimalDigit * radix + carry;
            decimalDigit = expanded % 10;
            carry = expanded / 10;
        }
        while (carry != 0) {
            decimalDigits.push_back(carry % 10);
            carry /= 10;
        }
    }

    while (decimalDigits.size() > 1 && decimalDigits.back() == 0) {
        decimalDigits.pop_back();
    }
    std::string result;
    result.reserve(decimalDigits.size());
    for (auto it = decimalDigits.rbegin(); it != decimalDigits.rend(); ++it) {
        result.push_back(static_cast<char>('0' + *it));
    }
    return result;
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
    const std::string canonical = canonicalTypeName(name);
    return canonical == "Float32" || canonical == "Float64";
}

NumericLiteralParts splitNumericLiteralText(const std::string& text) {
    const auto suffixPosition = exactNumericSuffixPosition(text);
    if (!suffixPosition.has_value()) {
        return {withoutSeparators(text), {}};
    }
    return {
        withoutSeparators(text.substr(0, *suffixPosition)),
        text.substr(*suffixPosition + 1)};
}

ParsedIntegerLiteral parseIntegerLiteralText(const std::string& text) {
    ParsedIntegerLiteral parsed;
    std::string_view digits(text);
    if (const auto suffixPosition = exactNumericSuffixPosition(text); suffixPosition.has_value()) {
        parsed.suffix = text.substr(*suffixPosition + 1);
        digits = std::string_view(text).substr(0, *suffixPosition);
    }

    if (digits.size() >= 2 && digits[0] == '0') {
        switch (digits[1]) {
        case 'x': parsed.radix = 16; digits.remove_prefix(2); break;
        case 'b': parsed.radix = 2; digits.remove_prefix(2); break;
        case 'o': parsed.radix = 8; digits.remove_prefix(2); break;
        default: break;
        }
    }

    if (digits.empty()) {
        return parsed;
    }

    bool previousWasDigit = false;
    bool sawDigit = false;
    std::string normalizedDigits;
    normalizedDigits.reserve(digits.size());
    for (size_t i = 0; i < digits.size(); ++i) {
        const char c = digits[i];
        if (c == '_') {
            if (!previousWasDigit || i + 1 >= digits.size()) {
                return parsed;
            }
            const int nextValue = digitValue(digits[i + 1]);
            if (nextValue < 0 || static_cast<unsigned>(nextValue) >= parsed.radix) {
                return parsed;
            }
            previousWasDigit = false;
            continue;
        }

        const int value = digitValue(c);
        if (value < 0 || static_cast<unsigned>(value) >= parsed.radix) {
            return parsed;
        }
        normalizedDigits += c;
        previousWasDigit = true;
        sawDigit = true;
    }
    if (!sawDigit || !previousWasDigit) {
        return parsed;
    }

    parsed.valid = true;
    parsed.magnitude = toDecimalMagnitude(normalizedDigits, parsed.radix);
    return parsed;
}

bool isValidFloatLiteralText(const std::string& text) {
    const auto suffixPosition = exactNumericSuffixPosition(text);
    const std::string_view magnitude = suffixPosition.has_value()
        ? std::string_view(text).substr(0, *suffixPosition)
        : std::string_view(text);
    size_t position = 0;
    if (!consumeDecimalDigits(magnitude, position)) {
        return false;
    }

    bool sawFractionOrExponent = false;
    if (position < magnitude.size() && magnitude[position] == '.') {
        sawFractionOrExponent = true;
        ++position;
        if (!consumeDecimalDigits(magnitude, position)) {
            return false;
        }
    }
    if (position < magnitude.size() && (magnitude[position] == 'e' || magnitude[position] == 'E')) {
        sawFractionOrExponent = true;
        ++position;
        if (position < magnitude.size() && (magnitude[position] == '+' || magnitude[position] == '-')) {
            ++position;
        }
        if (!consumeDecimalDigits(magnitude, position)) {
            return false;
        }
    }
    return sawFractionOrExponent && position == magnitude.size();
}

std::optional<ResolvedType> resolveNumericLiteralSuffixType(const std::string& suffix) {
    if (suffix.empty() || !isNumericTypeName(suffix)) {
        return std::nullopt;
    }
    return makePlainType(canonicalTypeName(suffix));
}

bool integerLiteralFitsTarget(const std::string& magnitude, const std::string& targetName, const TargetSpec& target) {
    UInt128 value = 0;
    if (!tryParseUnsignedDecimal(magnitude, &value)) {
        return false;
    }

    const unsigned pointerWidthBits = std::min(target.pointerWidthBits == 0 ? 64u : target.pointerWidthBits, 128u);
    if (targetName == "Char") {
        return value <= UInt128{0x10FFFF} &&
               !(value >= UInt128{0xD800} && value <= UInt128{0xDFFF});
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
        return value <= (UInt128{1} << 24);
    }
    if (targetName == "Float64") {
        return value <= (UInt128{1} << 53);
    }

    return false;
}

bool integerLiteralFitsNegatedTarget(const std::string& magnitude, const std::string& targetName, const TargetSpec& target) {
    if (targetName == "Char") {
        UInt128 value = 0;
        return tryParseUnsignedDecimal(magnitude, &value) && value == 0;
    }
    if (targetName.rfind("Int", 0) == 0) {
        const auto bits = integerLikeBitWidth(targetName);
        UInt128 value = 0;
        if (!bits.has_value() || !tryParseUnsignedDecimal(magnitude, &value)) {
            return false;
        }
        return value <= (UInt128{1} << (*bits - 1));
    }
    return integerLiteralFitsTarget(magnitude, targetName, target);
}

std::optional<ParsedFloatLiteral> parseFloatLiteralTarget(
    const std::string& magnitude,
    const std::string& targetName) {
    const std::string canonical = canonicalTypeName(targetName);
    if (!isFloatTypeName(canonical) || magnitude.empty()) {
        return std::nullopt;
    }

    const bool nonZeroMagnitude = literalMagnitudeHasNonZeroDigit(magnitude);

    if (canonical == "Float32") {
        float value = 0.0f;
        const auto parsed = std::from_chars(
            magnitude.data(), magnitude.data() + magnitude.size(), value, std::chars_format::general);
        if (parsed.ptr != magnitude.data() + magnitude.size() ||
            (parsed.ec != std::errc{} && (nonZeroMagnitude || parsed.ec != std::errc::result_out_of_range))) {
            return std::nullopt;
        }
        if (!std::isfinite(value) || (value == 0.0f && nonZeroMagnitude)) {
            return std::nullopt;
        }
        return ParsedFloatLiteral{std::bit_cast<std::uint32_t>(value), 32};
    }

    double value = 0.0;
    const auto parsed = std::from_chars(
        magnitude.data(), magnitude.data() + magnitude.size(), value, std::chars_format::general);
    if (parsed.ptr != magnitude.data() + magnitude.size() ||
        (parsed.ec != std::errc{} && (nonZeroMagnitude || parsed.ec != std::errc::result_out_of_range))) {
        return std::nullopt;
    }
    if (!std::isfinite(value) || (value == 0.0 && nonZeroMagnitude)) {
        return std::nullopt;
    }
    return ParsedFloatLiteral{std::bit_cast<std::uint64_t>(value), 64};
}

bool floatLiteralFitsTarget(const std::string& magnitude, const std::string& targetName) {
    return parseFloatLiteralTarget(magnitude, targetName).has_value();
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
