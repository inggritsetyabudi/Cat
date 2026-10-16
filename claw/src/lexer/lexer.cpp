#include "lexer/lexer.h"

#include <cctype>
#include <cstdint>
#include <unordered_map>

namespace claw::frontend {

namespace {
struct DecodedScalar {
    bool valid = false;
    uint32_t value = 0;
    size_t length = 1;
};

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

bool isUnicodeScalar(uint32_t value) {
    return value <= 0x10FFFF && !(value >= 0xD800 && value <= 0xDFFF);
}
} // namespace

Lexer::Lexer(std::string_view source) : source(source) {}

char Lexer::peek() const {
    return peekAhead(0);
}

char Lexer::peekAhead(size_t offset) const {
    const size_t index = pos + offset;
    if (index >= source.length()) return '\0';
    return source[index];
}

char Lexer::advance() {
    char c = source[pos++];
    if (c == '\n') {
        ++line;
        column = 1;
    } else {
        ++column;
    }
    return c;
}

bool Lexer::isAtEnd() const {
    return pos >= source.length();
}

bool Lexer::skipBlockComment() {
    if (peek() != '/' || peekAhead(1) != '*') {
        return false;
    }

    advance();
    advance();
    size_t depth = 1;
    while (!isAtEnd() && depth > 0) {
        if (peek() == '/' && peekAhead(1) == '*') {
            advance();
            advance();
            ++depth;
            continue;
        }
        if (peek() == '*' && peekAhead(1) == '/') {
            advance();
            advance();
            --depth;
            continue;
        }
        advance();
    }
    return true;
}

void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
        const unsigned char c = static_cast<unsigned char>(peek());
        if (std::isspace(c)) {
            advance();
            continue;
        }
        if (peek() == '/' && peekAhead(1) == '/' &&
            !(braceDepth == 0 && peekAhead(2) == '/')) {
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
            continue;
        }
        if (skipBlockComment()) {
            continue;
        }
        break;
    }
}

Token Lexer::readDocComment() {
    const size_t startLine = line;
    const size_t startColumn = column;
    const size_t startPos = pos;
    advance();
    advance();
    advance();

    std::string text;
    while (!isAtEnd() && peek() != '\n' && peek() != '\r') {
        text += advance();
    }
    return makeToken(TokenKind::DocComment, std::move(text), startLine, startColumn, pos - startPos);
}

Token Lexer::makeToken(
    TokenKind kind,
    std::string text,
    size_t startLine,
    size_t startColumn,
    size_t tokenLength) const {
    return Token{kind, std::move(text), startLine, startColumn, tokenLength};
}

Token Lexer::readIdentifierOrKeyword() {
    const size_t startLine = line;
    const size_t startColumn = column;
    const size_t startPos = pos;

    std::string text;
    while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        text += advance();
    }

    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"fn", TokenKind::KwFn}, {"val", TokenKind::KwVal}, {"var", TokenKind::KwVar},
        {"return", TokenKind::KwReturn},
        {"if", TokenKind::KwIf}, {"else", TokenKind::KwElse},
        {"loop", TokenKind::KwLoop}, {"scan", TokenKind::KwScan}, {"stop", TokenKind::KwStop},
        {"skip", TokenKind::KwSkip}, {"shape", TokenKind::KwShape}, {"choice", TokenKind::KwChoice},
        {"view", TokenKind::KwView}, {"scope", TokenKind::KwScope},
        {"import", TokenKind::KwImport}, {"share", TokenKind::KwShare}, {"super", TokenKind::KwSuper},
        {"pick", TokenKind::KwPick}, {"raw", TokenKind::KwRaw},
        {"as", TokenKind::KwAs}, {"over", TokenKind::KwOver},
        {"and", TokenKind::KwAnd}, {"or", TokenKind::KwOr}, {"not", TokenKind::KwNot},
        {"in", TokenKind::KwIn},
        {"fail", TokenKind::KwFail}, {"ref", TokenKind::KwRef}, {"mut", TokenKind::KwMut}, {"try", TokenKind::KwTry},
        {"foreign", TokenKind::KwForeign}, {"static", TokenKind::KwStatic},
        {"contract", TokenKind::KwContract}, {"implements", TokenKind::KwImplements},
        {"with", TokenKind::KwWith}, {"self", TokenKind::KwSelf},
        {"true", TokenKind::KwTrue}, {"false", TokenKind::KwFalse}
    };

    const auto it = keywords.find(text);
    return makeToken(
        it != keywords.end() ? it->second : TokenKind::Identifier,
        text,
        startLine,
        startColumn,
        pos - startPos);
}

Token Lexer::readNumber(bool allowFraction) {
    const size_t startLine = line;
    const size_t startColumn = column;
    const size_t startPos = pos;

    std::string text;

    if (peek() == '0' && (peekAhead(1) == 'x' || peekAhead(1) == 'b' || peekAhead(1) == 'o')) {
        text += advance();
        text += advance();
        while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            text += advance();
        }
        return makeToken(TokenKind::IntLiteral, text, startLine, startColumn, pos - startPos);
    }

    const auto readDecimalDigitsAndSeparators = [&]() {
        while (!isAtEnd()) {
            const unsigned char current = static_cast<unsigned char>(peek());
            if (std::isdigit(current)) {
                text += advance();
                continue;
            }
            if (peek() == '_' && !std::isalpha(static_cast<unsigned char>(peekAhead(1)))) {
                text += advance();
                continue;
            }
            break;
        }
    };
    readDecimalDigitsAndSeparators();

    bool isFloat = false;
    if (allowFraction && !isAtEnd() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peekAhead(1)))) {
        isFloat = true;
        text += advance();
        readDecimalDigitsAndSeparators();
    }

    const auto hasExponent = [&]() {
        if (isAtEnd() || (peek() != 'e' && peek() != 'E')) {
            return false;
        }

        size_t nextPos = pos + 1;
        if (nextPos < source.length() && (source[nextPos] == '+' || source[nextPos] == '-')) {
            ++nextPos;
        }

        return nextPos < source.length() && std::isdigit(static_cast<unsigned char>(source[nextPos]));
    };

    if (hasExponent()) {
        isFloat = true;
        text += advance();
        if (!isAtEnd() && (peek() == '+' || peek() == '-')) {
            text += advance();
        }
        readDecimalDigitsAndSeparators();
    }

    if (!isAtEnd() && peek() == '_' && std::isalpha(static_cast<unsigned char>(peekAhead(1)))) {
        text += advance();
        while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            text += advance();
        }
    }

    return makeToken(isFloat ? TokenKind::FloatLiteral : TokenKind::IntLiteral, text, startLine, startColumn, pos - startPos);
}

Token Lexer::readString() {
    const size_t startLine = line;
    const size_t startColumn = column;
    const size_t startPos = pos;

    advance();
    std::string text;
    while (!isAtEnd() && peek() != '"') {
        text += advance();
    }
    if (!isAtEnd()) {
        advance();
    }
    return makeToken(TokenKind::StringLiteral, text, startLine, startColumn, pos - startPos);
}

Token Lexer::readChar() {
    const size_t startLine = line;
    const size_t startColumn = column;
    const size_t startPos = pos;

    advance();
    uint32_t scalarValue = 0;
    size_t scalarCount = 0;
    std::string errorMessage;
    const auto setError = [&](const std::string& message) {
        if (errorMessage.empty()) {
            errorMessage = message;
        }
    };

    while (!isAtEnd() && peek() != '\'' && peek() != '\n') {
        if (peek() == '\\') {
            advance();
            if (isAtEnd() || peek() == '\n') {
                setError("Malformed escape sequence in character literal.");
                break;
            }
            const char escape = advance();
            uint32_t escapedValue = 0;
            bool escapeValid = true;
            switch (escape) {
            case 'n': escapedValue = '\n'; break;
            case 't': escapedValue = '\t'; break;
            case 'r': escapedValue = '\r'; break;
            case '\\': escapedValue = '\\'; break;
            case '\'': escapedValue = '\''; break;
            case '"': escapedValue = '"'; break;
            case '0': escapedValue = 0; break;
            case 'u': {
                if (isAtEnd() || peek() != '{') {
                    setError("Malformed Unicode escape in character literal.");
                    escapeValid = false;
                    break;
                }
                advance();
                bool sawDigit = false;
                bool malformed = false;
                uint32_t codePoint = 0;
                bool outOfRange = false;
                while (!isAtEnd() && peek() != '}' && peek() != '\'' && peek() != '\n') {
                    const unsigned char digitChar = static_cast<unsigned char>(advance());
                    uint32_t digit = 0;
                    if (digitChar >= '0' && digitChar <= '9') {
                        digit = digitChar - '0';
                    } else if (digitChar >= 'a' && digitChar <= 'f') {
                        digit = digitChar - 'a' + 10;
                    } else if (digitChar >= 'A' && digitChar <= 'F') {
                        digit = digitChar - 'A' + 10;
                    } else {
                        malformed = true;
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
                }
                if (!isAtEnd() && peek() == '}') {
                    advance();
                } else {
                    malformed = true;
                }
                if (!sawDigit || malformed) {
                    setError("Malformed Unicode escape in character literal.");
                    escapeValid = false;
                } else if (outOfRange || !isUnicodeScalar(codePoint)) {
                    setError("Unicode escape is not a valid Unicode scalar value.");
                    escapeValid = false;
                } else {
                    escapedValue = codePoint;
                }
                break;
            }
            default:
                setError("Unknown escape sequence in character literal.");
                escapeValid = false;
                break;
            }
            if (escapeValid) {
                if (scalarCount == 0) {
                    scalarValue = escapedValue;
                }
                ++scalarCount;
            }
            continue;
        }

        const DecodedScalar decoded = decodeUtf8Scalar(source, pos);
        if (!decoded.valid) {
            setError("Invalid UTF-8 in character literal.");
            const size_t consumeLength = decoded.length == 0 ? 1 : decoded.length;
            for (size_t i = 0; i < consumeLength && !isAtEnd() && peek() != '\'' && peek() != '\n'; ++i) {
                advance();
            }
            continue;
        }
        if (scalarCount == 0) {
            scalarValue = decoded.value;
        }
        ++scalarCount;
        for (size_t i = 0; i < decoded.length; ++i) {
            advance();
        }
    }

    const bool terminated = !isAtEnd() && peek() == '\'';
    if (terminated) {
        advance();
    }
    if (!terminated && errorMessage.empty()) {
        errorMessage = "Unterminated character literal.";
    } else if (errorMessage.empty() && scalarCount != 1) {
        errorMessage = "Character literal must contain exactly one Unicode scalar value.";
    }

    return makeToken(
        errorMessage.empty() ? TokenKind::CharLiteral : TokenKind::CharError,
        errorMessage.empty() ? std::to_string(scalarValue) : errorMessage,
        startLine,
        startColumn,
        pos - startPos);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (!isAtEnd()) {
        skipWhitespace();
        if (isAtEnd()) {
            break;
        }

        if (braceDepth == 0 && peek() == '/' && peekAhead(1) == '/' && peekAhead(2) == '/') {
            tokens.push_back(readDocComment());
            continue;
        }

        const size_t startLine = line;
        const size_t startColumn = column;
        const size_t startPos = pos;
        const char c = peek();

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(readIdentifierOrKeyword());
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            const bool allowFraction = tokens.empty() || tokens.back().kind != TokenKind::Dot;
            tokens.push_back(readNumber(allowFraction));
            continue;
        }
        if (c == '"') {
            tokens.push_back(readString());
            continue;
        }
        if (c == '\'') {
            tokens.push_back(readChar());
            continue;
        }

        advance();
        switch (c) {
            case '[':
                tokens.push_back(makeToken(TokenKind::LBracket, "[", startLine, startColumn, 1));
                break;
            case ']':
                tokens.push_back(makeToken(TokenKind::RBracket, "]", startLine, startColumn, 1));
                break;
            case '{':
                tokens.push_back(makeToken(TokenKind::LBrace, "{", startLine, startColumn, 1));
                break;
            case '}':
                tokens.push_back(makeToken(TokenKind::RBrace, "}", startLine, startColumn, 1));
                break;
            case '(':
                tokens.push_back(makeToken(TokenKind::LParen, "(", startLine, startColumn, 1));
                break;
            case ')':
                tokens.push_back(makeToken(TokenKind::RParen, ")", startLine, startColumn, 1));
                break;
            case ':':
                tokens.push_back(makeToken(TokenKind::Colon, ":", startLine, startColumn, 1));
                break;
            case '=':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::EqEq, "==", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Equal, "=", startLine, startColumn, 1));
                }
                break;
            case '-':
                if (!isAtEnd() && peek() == '>') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::Arrow, "->", startLine, startColumn, pos - startPos));
                } else if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::MinusEqual, "-=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Minus, "-", startLine, startColumn, 1));
                }
                break;
            case ',':
                tokens.push_back(makeToken(TokenKind::Comma, ",", startLine, startColumn, 1));
                break;
            case '.':
                if (!isAtEnd() && peek() == '.') {
                    advance();
                    if (!isAtEnd() && peek() == '=') {
                        advance();
                        tokens.push_back(makeToken(TokenKind::DotDotEqual, "..=", startLine, startColumn, pos - startPos));
                    } else {
                        tokens.push_back(makeToken(TokenKind::DotDot, "..", startLine, startColumn, pos - startPos));
                    }
                } else {
                    tokens.push_back(makeToken(TokenKind::Dot, ".", startLine, startColumn, 1));
                }
                break;
            case '!':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::NotEq, "!=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Bang, "!", startLine, startColumn, 1));
                }
                break;
            case '<':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::LessEq, "<=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Less, "<", startLine, startColumn, 1));
                }
                break;
            case '>':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::GreaterEq, ">=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Greater, ">", startLine, startColumn, 1));
                }
                break;
            case '+':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::PlusEqual, "+=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Plus, "+", startLine, startColumn, 1));
                }
                break;
            case '*':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::StarEqual, "*=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Star, "*", startLine, startColumn, 1));
                }
                break;
            case '/':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::SlashEqual, "/=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Slash, "/", startLine, startColumn, 1));
                }
                break;
            case '%':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::PercentEqual, "%=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Percent, "%", startLine, startColumn, 1));
                }
                break;
            default:
                tokens.push_back(makeToken(TokenKind::Error, std::string(1, c), startLine, startColumn, 1));
                break;
        }

        if (!tokens.empty() && tokens.back().kind == TokenKind::LBrace) {
            ++braceDepth;
        } else if (!tokens.empty() && tokens.back().kind == TokenKind::RBrace && braceDepth > 0) {
            --braceDepth;
        }
    }

    tokens.push_back(makeToken(TokenKind::Eof, "", line, column, 1));
    return tokens;
}

} // namespace claw::frontend
