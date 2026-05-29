#include "lexer.h"
#include <cctype>
#include <unordered_map>

namespace db {

static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"CREATE", TokenType::KW_CREATE},
    {"DROP", TokenType::KW_DROP},
    {"USE", TokenType::KW_USE},
    {"DATABASE", TokenType::KW_DATABASE},
    {"TABLE", TokenType::KW_TABLE},
    {"INSERT", TokenType::KW_INSERT},
    {"INTO", TokenType::KW_INTO},
    {"VALUE", TokenType::KW_VALUE},
    {"UPDATE", TokenType::KW_UPDATE},
    {"SET", TokenType::KW_SET},
    {"WHERE", TokenType::KW_WHERE},
    {"DELETE", TokenType::KW_DELETE},
    {"SELECT", TokenType::KW_SELECT},
    {"FROM", TokenType::KW_FROM},
    {"AS", TokenType::KW_AS},
    {"AND", TokenType::KW_AND},
    {"OR", TokenType::KW_OR},
    {"NOT", TokenType::KW_NOT},
    {"NULL", TokenType::KW_NULL},
    {"BETWEEN", TokenType::KW_BETWEEN},
    {"LIKE", TokenType::KW_LIKE},
    {"NOT_NULL", TokenType::KW_NOT_NULL},
    {"INDEXED", TokenType::KW_INDEXED},
    {"DEFAULT", TokenType::KW_DEFAULT},
    {"SUM", TokenType::KW_SUM},
    {"COUNT", TokenType::KW_COUNT},
    {"AVG", TokenType::KW_AVG},
    {"INT", TokenType::KW_INT},
    {"STRING", TokenType::KW_STRING},
};

Lexer::Lexer(std::string input) : _input(std::move(input)) {}

char Lexer::peek(size_t ahead) const {
    size_t idx = _pos + ahead;
    if (idx >= _input.size()) return '\0';
    return _input[idx];
}

char Lexer::advance() {
    char c = _input[_pos++];
    if (c == '\n') { ++_line; _col = 1; }
    else { ++_col; }
    return c;
}

void Lexer::skip_whitespace_and_comments() {
    while (_pos < _input.size()) {
        char c = peek();
        if (std::isspace(static_cast<unsigned char>(c))) {
            advance();
        } else if (c == '-' && peek(1) == '-') {
            // single-line comment
            while (_pos < _input.size() && peek() != '\n') advance();
        } else if (c == '/' && peek(1) == '*') {
            advance(); advance();
            while (_pos + 1 < _input.size()) {
                if (peek() == '*' && peek(1) == '/') { advance(); advance(); break; }
                advance();
            }
        } else {
            break;
        }
    }
}

Token Lexer::read_string_literal() {
    size_t ln = _line, cl = _col;
    advance(); // consume opening "
    std::string val;
    while (_pos < _input.size() && peek() != '"') {
        char c = advance();
        if (c == '\\') {
            char esc = advance();
            switch (esc) {
                case 'n': val += '\n'; break;
                case 't': val += '\t'; break;
                case '"': val += '"';  break;
                case '\\': val += '\\'; break;
                default: val += esc;  break;
            }
        } else {
            val += c;
        }
    }
    if (_pos >= _input.size())
        throw LexError("Unterminated string literal at line " + std::to_string(ln));
    advance(); // consume closing "
    return {TokenType::LIT_STRING, val, ln, cl};
}

Token Lexer::read_number() {
    size_t ln = _line, cl = _col;
    std::string val;
    if (peek() == '-') { val += advance(); }
    while (_pos < _input.size() && std::isdigit(static_cast<unsigned char>(peek())))
        val += advance();
    return {TokenType::LIT_INT, val, ln, cl};
}

Token Lexer::read_ident_or_keyword() {
    size_t ln = _line, cl = _col;
    std::string val;
    while (_pos < _input.size()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            val += advance();
        else
            break;
    }
    // case-insensitive keyword match
    std::string upper;
    upper.reserve(val.size());
    for (char c : val) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    auto kit = KEYWORDS.find(upper);
    if (kit != KEYWORDS.end()) return {kit->second, val, ln, cl};
    return {TokenType::IDENT, val, ln, cl};
}

Token Lexer::read_operator_or_punct() {
    size_t ln = _line, cl = _col;
    char c = advance();
    switch (c) {
        case '(': return {TokenType::LPAREN, "(", ln, cl};
        case ')': return {TokenType::RPAREN, ")", ln, cl};
        case ',': return {TokenType::COMMA, ",", ln, cl};
        case ';': return {TokenType::SEMICOLON, ";", ln, cl};
        case '*': return {TokenType::STAR, "*", ln, cl};
        case '.': return {TokenType::DOT, ".", ln, cl};
        case '=':
            if (peek() == '=') { advance(); return {TokenType::OP_EQ, "==", ln, cl}; }
            return {TokenType::OP_ASSIGN, "=", ln, cl};
        case '!':
            if (peek() == '=') { advance(); return {TokenType::OP_NEQ, "!=", ln, cl}; }
            throw LexError("Unexpected character '!' at line " + std::to_string(ln));
        case '<':
            if (peek() == '=') { advance(); return {TokenType::OP_LE, "<=", ln, cl}; }
            return {TokenType::OP_LT, "<", ln, cl};
        case '>':
            if (peek() == '=') { advance(); return {TokenType::OP_GE, ">=", ln, cl}; }
            return {TokenType::OP_GT, ">", ln, cl};
        default:
            throw LexError(std::string("Unexpected character '") + c +
                           "' at line " + std::to_string(ln));
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        skip_whitespace_and_comments();
        if (_pos >= _input.size()) break;
        char c = peek();
        if (c == '"') {
            tokens.push_back(read_string_literal());
        } else if (std::isdigit(static_cast<unsigned char>(c)) ||
                   (c == '-' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
            tokens.push_back(read_number());
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(read_ident_or_keyword());
        } else {
            tokens.push_back(read_operator_or_punct());
        }
    }
    tokens.push_back({TokenType::END, "", _line, _col});
    return tokens;
}

} // namespace db
