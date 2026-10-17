#include "lexer/literal_escapes.h"

namespace claw::frontend::literal_escapes {

DecodedScalar decodeUtf8Scalar(std::string_view source, size_t offset) {
    const auto byteAt = [&](size_t index) {
        return static_cast<unsigned char>(source[index]);
    };
    if (offset >= source.size()) {
        return {};
    }

    const unsigned char first = byteAt(offset);
    if (first <= 0x7F) {
        return {true, first, 1};
    }

    size_t length = 0;
    uint32_t value = 0;
    uint32_t minimum = 0;
    if (first >= 0xC2 && first <= 0xDF) {
        length = 2;
        value = first & 0x1F;
        minimum = 0x80;
    } else if (first >= 0xE0 && first <= 0xEF) {
        length = 3;
        value = first & 0x0F;
        minimum = 0x800;
    } else if (first >= 0xF0 && first <= 0xF4) {
        length = 4;
        value = first & 0x07;
        minimum = 0x10000;
    } else {
        return {false, 0, 1};
    }

    if (offset + length > source.size()) {
        return {false, 0, 1};
    }
    for (size_t i = 1; i < length; ++i) {
        const unsigned char continuation = byteAt(offset + i);
        if ((continuation & 0xC0) != 0x80) {
            return {false, 0, 1};
        }
        value = (value << 6) | (continuation & 0x3F);
    }

    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return {false, 0, length};
    }
    return {true, value, length};
}

DecodedEscape decodeEscape(std::string_view source, size_t offset, bool stopAtCharDelimiter) {
    if (offset >= source.size() || source[offset] != '\\') {
        return {EscapeError::MalformedEscape, 0, 0};
    }
    if (offset + 1 >= source.size()) {
        return {EscapeError::MalformedEscape, 0, 1};
    }
    if (stopAtCharDelimiter && source[offset + 1] == '\n') {
        return {EscapeError::MalformedEscape, 0, 1};
    }

    const char escape = source[offset + 1];
    switch (escape) {
    case 'n': return {EscapeError::None, '\n', 2};
    case 't': return {EscapeError::None, '\t', 2};
    case 'r': return {EscapeError::None, '\r', 2};
    case '\\': return {EscapeError::None, '\\', 2};
    case '\'': return {EscapeError::None, '\'', 2};
    case '"': return {EscapeError::None, '"', 2};
    case '0': return {EscapeError::None, 0, 2};
    case 'u': {
        if (offset + 2 >= source.size() || source[offset + 2] != '{') {
            return {EscapeError::MalformedUnicode, 0, 2};
        }

        size_t index = offset + 3;
        bool sawDigit = false;
        bool malformed = false;
        bool outOfRange = false;
        bool closed = false;
        uint32_t codePoint = 0;
        while (index < source.size()) {
            const unsigned char digitChar = static_cast<unsigned char>(source[index]);
            if (digitChar == '}') {
                ++index;
                closed = true;
                break;
            }
            if (digitChar == '\n' || (stopAtCharDelimiter && digitChar == '\'')) {
                break;
            }

            uint32_t digit = 0;
            if (digitChar >= '0' && digitChar <= '9') {
                digit = digitChar - '0';
            } else if (digitChar >= 'a' && digitChar <= 'f') {
                digit = digitChar - 'a' + 10;
            } else if (digitChar >= 'A' && digitChar <= 'F') {
                digit = digitChar - 'A' + 10;
            } else {
                malformed = true;
                ++index;
                continue;
            }

            sawDigit = true;
            if (codePoint > 0x10FFFF / 16) {
                outOfRange = true;
            } else {
                codePoint = codePoint * 16 + digit;
                if (codePoint > 0x10FFFF) {
                    outOfRange = true;
                }
            }
            ++index;
        }

        const size_t consumed = index - offset;
        if (!sawDigit || malformed || !closed) {
            return {EscapeError::MalformedUnicode, 0, consumed};
        }
        if (outOfRange || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
            return {EscapeError::InvalidScalar, 0, consumed};
        }
        return {EscapeError::None, codePoint, consumed};
    }
    default:
        return {EscapeError::UnknownEscape, 0, 2};
    }
}

DecodedString decodeStringContent(std::string_view source) {
    DecodedString decoded;
    decoded.value.reserve(source.size());
    for (size_t index = 0; index < source.size();) {
        if (source[index] == '\\') {
            const DecodedEscape escape = decodeEscape(source, index);
            if (!escape.valid()) {
                decoded.error = escape.error;
                decoded.errorOffset = index;
                return decoded;
            }
            appendUtf8Scalar(decoded.value, escape.value);
            index += escape.length;
            continue;
        }

        const DecodedScalar scalar = decodeUtf8Scalar(source, index);
        if (!scalar.valid) {
            decoded.error = EscapeError::InvalidUtf8;
            decoded.errorOffset = index;
            return decoded;
        }
        decoded.value.append(source.substr(index, scalar.length));
        index += scalar.length;
    }
    return decoded;
}

std::string errorMessage(EscapeError error, LiteralKind kind) {
    const bool isString = kind == LiteralKind::String;
    switch (error) {
    case EscapeError::MalformedEscape:
        return isString ? "Malformed escape sequence in string literal." :
                          "Malformed escape sequence in character literal.";
    case EscapeError::MalformedUnicode:
        return isString ? "Malformed Unicode escape in string literal." :
                          "Malformed Unicode escape in character literal.";
    case EscapeError::UnknownEscape:
        return isString ? "Unknown escape sequence in string literal." :
                          "Unknown escape sequence in character literal.";
    case EscapeError::InvalidScalar:
        return "Unicode escape is not a valid Unicode scalar value.";
    case EscapeError::InvalidUtf8:
        return isString ? "Invalid UTF-8 in string literal." :
                          "Invalid UTF-8 in character literal.";
    case EscapeError::None:
        return {};
    }
    return {};
}

void appendUtf8Scalar(std::string& output, uint32_t value) {
    if (value <= 0x7F) {
        output.push_back(static_cast<char>(value));
    } else if (value <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else if (value <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (value >> 18)));
        output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    }
}

} // namespace claw::frontend::literal_escapes
