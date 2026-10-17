#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace claw::frontend::literal_escapes {

enum class LiteralKind {
    Character,
    String,
};

enum class EscapeError {
    None,
    MalformedEscape,
    MalformedUnicode,
    UnknownEscape,
    InvalidScalar,
    InvalidUtf8,
};

struct DecodedScalar {
    bool valid = false;
    uint32_t value = 0;
    size_t length = 1;
};

struct DecodedEscape {
    EscapeError error = EscapeError::None;
    uint32_t value = 0;
    size_t length = 0;

    bool valid() const { return error == EscapeError::None; }
};

struct DecodedString {
    std::string value;
    EscapeError error = EscapeError::None;
    size_t errorOffset = 0;

    bool valid() const { return error == EscapeError::None; }
};

DecodedScalar decodeUtf8Scalar(std::string_view source, size_t offset);
DecodedEscape decodeEscape(std::string_view source, size_t offset, bool stopAtCharDelimiter = false);
DecodedString decodeStringContent(std::string_view source);
std::string errorMessage(EscapeError error, LiteralKind kind);
void appendUtf8Scalar(std::string& output, uint32_t value);

} // namespace claw::frontend::literal_escapes
