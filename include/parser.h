#pragma once
#include "lexer.h"
#include "ast.h"
#include <vector>
#include <stdexcept>

namespace db {

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Recursive-descent parser over a flat token vector.
// parse_all() returns all statements found before END.
class Parser {
    std::vector<Token> _tokens;
    size_t _pos = 0;

public:
    explicit Parser(std::vector<Token> tokens);

    std::vector<Statement> parse_all();

private:
    const Token& peek(size_t ahead = 0) const;
    const Token& consume();
    const Token& expect(TokenType t);
    bool check(TokenType t) const;
    bool match(TokenType t);

    Statement parse_statement();

    // DB-level
    Statement parse_create();
    Statement parse_drop();
    Statement parse_use();

    // DDL
    CreateTableStmt parse_create_table();
    ColDefNode parse_col_def();

    // DML
    InsertStmt parse_insert();
    UpdateStmt parse_update();
    DeleteStmt parse_delete();
    SelectStmt parse_select();

    SelectCol   parse_select_col();

    // Condition grammar
    Expr parse_condition();  // OR
    Expr parse_and();        // AND
    Expr parse_primary_cond();

    // Value expression (not boolean)
    Expr parse_value_expr();

    std::string parse_table_ref(); // db.table or just table
};

} // namespace db
