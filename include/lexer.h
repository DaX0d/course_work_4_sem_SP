#pragma once
#include <string>
#include <vector>
#include <stdexcept>

namespace db {

enum class TokenType {
    KW_CREATE, KW_DROP, KW_USE, KW_DATABASE, KW_TABLE,
    KW_INSERT, KW_INTO, KW_VALUE, KW_UPDATE, KW_SET,
    KW_WHERE,  KW_DELETE, KW_SELECT, KW_FROM, KW_AS,
    KW_AND, KW_OR, KW_NOT, KW_NULL, KW_BETWEEN, KW_LIKE,
    KW_NOT_NULL, KW_INDEXED, KW_DEFAULT,
    KW_SUM, KW_COUNT, KW_AVG,
    KW_INT, KW_STRING,
    LIT_INT,
    LIT_STRING,
    IDENT,
    OP_EQ,     // ==
    OP_NEQ,    // !=
    OP_LT,     // <
    OP_GT,     // >
    OP_LE,     // <=
    OP_GE,     // >=
    OP_ASSIGN, // =
    LPAREN,    // (
    RPAREN,    // )
    COMMA,     // ,
    SEMICOLON, // ;
    STAR,      // *
    DOT,       // .
    END
};

struct Token {
    TokenType type;
    std::string value;
    size_t line = 1;
    size_t col  = 1;
};

class LexError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Lexer {
    std::string _input;
    size_t _pos  = 0;
    size_t _line = 1;
    size_t _col  = 1;

public:
    explicit Lexer(std::string input);
    std::vector<Token> tokenize();

private:
    char peek(size_t ahead = 0) const;
    char advance();
    void skip_whitespace_and_comments();
    Token read_string_literal();
    Token read_number();
    Token read_ident_or_keyword();
    Token read_operator_or_punct();
};

} // namespace db
