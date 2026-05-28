#include "parser.h"
#include <sstream>

namespace db {

Parser::Parser(std::vector<Token> tokens) : _tokens(std::move(tokens)) {}

// ── token helpers ─────────────────────────────────────────────────────────────

const Token& Parser::peek(size_t ahead) const {
    size_t idx = _pos + ahead;
    if (idx >= _tokens.size()) return _tokens.back(); // END
    return _tokens[idx];
}

const Token& Parser::consume() {
    const Token& t = _tokens[_pos];
    if (t.type != TokenType::END) ++_pos;
    return t;
}

const Token& Parser::expect(TokenType t) {
    if (!check(t)) {
        const Token& cur = peek();
        throw ParseError("Expected token type " + std::to_string(static_cast<int>(t)) +
                         " but got '" + cur.value + "' at line " + std::to_string(cur.line));
    }
    return consume();
}

bool Parser::check(TokenType t) const {
    return peek().type == t;
}

bool Parser::match(TokenType t) {
    if (check(t)) { consume(); return true; }
    return false;
}

// ── top-level ─────────────────────────────────────────────────────────────────

std::vector<Statement> Parser::parse_all() {
    std::vector<Statement> stmts;
    while (!check(TokenType::END))
        stmts.push_back(parse_statement());
    return stmts;
}

Statement Parser::parse_statement() {
    const Token& t = peek();
    switch (t.type) {
        case TokenType::KW_CREATE: return parse_create();
        case TokenType::KW_DROP:   return parse_drop();
        case TokenType::KW_USE:    return parse_use();
        case TokenType::KW_INSERT: return parse_insert();
        case TokenType::KW_UPDATE: return parse_update();
        case TokenType::KW_DELETE: return parse_delete();
        case TokenType::KW_SELECT: return parse_select();
        default:
            throw ParseError("Unexpected token '" + t.value +
                             "' at line " + std::to_string(t.line));
    }
}

// ── DB statements ─────────────────────────────────────────────────────────────

Statement Parser::parse_create() {
    consume(); // CREATE
    if (check(TokenType::KW_DATABASE)) {
        consume();
        std::string name = expect(TokenType::IDENT).value;
        expect(TokenType::SEMICOLON);
        return CreateDatabaseStmt{name};
    }
    if (check(TokenType::KW_TABLE)) {
        return parse_create_table();
    }
    throw ParseError("Expected DATABASE or TABLE after CREATE");
}

Statement Parser::parse_drop() {
    consume(); // DROP
    if (check(TokenType::KW_DATABASE)) {
        consume();
        std::string name = expect(TokenType::IDENT).value;
        expect(TokenType::SEMICOLON);
        return DropDatabaseStmt{name};
    }
    if (check(TokenType::KW_TABLE)) {
        consume();
        std::string name = expect(TokenType::IDENT).value;
        expect(TokenType::SEMICOLON);
        return DropTableStmt{name};
    }
    throw ParseError("Expected DATABASE or TABLE after DROP");
}

Statement Parser::parse_use() {
    consume(); // USE
    std::string name = expect(TokenType::IDENT).value;
    expect(TokenType::SEMICOLON);
    return UseDatabaseStmt{name};
}

// ── DDL ───────────────────────────────────────────────────────────────────────

CreateTableStmt Parser::parse_create_table() {
    expect(TokenType::KW_TABLE);
    std::string tname = expect(TokenType::IDENT).value;
    expect(TokenType::LPAREN);
    std::vector<ColDefNode> cols;
    cols.push_back(parse_col_def());
    while (match(TokenType::COMMA))
        cols.push_back(parse_col_def());
    expect(TokenType::RPAREN);
    expect(TokenType::SEMICOLON);
    return {tname, std::move(cols)};
}

ColDefNode Parser::parse_col_def() {
    ColDefNode cd;
    cd.name = expect(TokenType::IDENT).value;
    if (check(TokenType::KW_INT)) {
        consume(); cd.type = ColumnType::INT;
    } else if (check(TokenType::KW_STRING)) {
        consume(); cd.type = ColumnType::STRING;
    } else {
        throw ParseError("Expected INT or STRING for column type");
    }
    // optional constraints (any order, any number)
    while (true) {
        if (check(TokenType::KW_NOT_NULL))  { consume(); cd.not_null = true; }
        else if (check(TokenType::KW_INDEXED)) { consume(); cd.indexed = true; }
        else if (check(TokenType::KW_DEFAULT)) {
            consume();
            // task 10: parse default value
            if (check(TokenType::KW_NULL)) {
                consume(); cd.default_val = NullValue{};
            } else if (check(TokenType::LIT_INT)) {
                cd.default_val = static_cast<int64_t>(
                    std::stoll(consume().value));
            } else if (check(TokenType::LIT_STRING)) {
                cd.default_val = consume().value;
            } else {
                throw ParseError("Expected literal after DEFAULT");
            }
        } else break;
    }
    return cd;
}

// ── DML ───────────────────────────────────────────────────────────────────────

InsertStmt Parser::parse_insert() {
    expect(TokenType::KW_INSERT);
    expect(TokenType::KW_INTO);
    std::string tname = parse_table_ref();
    expect(TokenType::LPAREN);

    std::vector<std::string> cols;
    cols.push_back(expect(TokenType::IDENT).value);
    while (match(TokenType::COMMA))
        cols.push_back(expect(TokenType::IDENT).value);
    expect(TokenType::RPAREN);

    expect(TokenType::KW_VALUE);

    std::vector<std::vector<Expr>> rows;
    auto parse_row = [&] {
        expect(TokenType::LPAREN);
        std::vector<Expr> row;
        row.push_back(parse_value_expr());
        while (match(TokenType::COMMA))
            row.push_back(parse_value_expr());
        expect(TokenType::RPAREN);
        return row;
    };
    rows.push_back(parse_row());
    while (match(TokenType::COMMA))
        rows.push_back(parse_row());

    expect(TokenType::SEMICOLON);
    return {tname, std::move(cols), std::move(rows)};
}

UpdateStmt Parser::parse_update() {
    expect(TokenType::KW_UPDATE);
    std::string tname = parse_table_ref();
    expect(TokenType::KW_SET);

    std::vector<std::pair<std::string, Expr>> asgn;
    auto parse_one = [&] {
        std::string col = expect(TokenType::IDENT).value;
        expect(TokenType::OP_ASSIGN);
        Expr val = parse_value_expr();
        asgn.push_back({col, std::move(val)});
    };
    parse_one();
    while (match(TokenType::COMMA)) parse_one();

    std::optional<Expr> where;
    if (match(TokenType::KW_WHERE))
        where = parse_condition();

    expect(TokenType::SEMICOLON);
    return {tname, std::move(asgn), std::move(where)};
}

DeleteStmt Parser::parse_delete() {
    expect(TokenType::KW_DELETE);
    expect(TokenType::KW_FROM);
    std::string tname = parse_table_ref();

    std::optional<Expr> where;
    if (match(TokenType::KW_WHERE))
        where = parse_condition();

    expect(TokenType::SEMICOLON);
    return {tname, std::move(where)};
}

SelectStmt Parser::parse_select() {
    expect(TokenType::KW_SELECT);

    std::vector<SelectCol> cols;
    if (check(TokenType::STAR)) {
        consume();
        cols.push_back(StarCol{});
    } else {
        cols.push_back(parse_select_col());
        while (match(TokenType::COMMA))
            cols.push_back(parse_select_col());
    }

    expect(TokenType::KW_FROM);
    std::string tname = parse_table_ref();

    std::optional<Expr> where;
    if (match(TokenType::KW_WHERE))
        where = parse_condition();

    expect(TokenType::SEMICOLON);
    return {tname, std::move(cols), std::move(where)};
}

SelectCol Parser::parse_select_col() {
    // aggregate function: SUM(...) / COUNT(*) / AVG(...)
    AggFunc func;
    bool is_agg = false;
    if (check(TokenType::KW_SUM))   { consume(); func = AggFunc::SUM;   is_agg = true; }
    else if (check(TokenType::KW_COUNT)) { consume(); func = AggFunc::COUNT; is_agg = true; }
    else if (check(TokenType::KW_AVG))   { consume(); func = AggFunc::AVG;   is_agg = true; }

    if (is_agg) {
        expect(TokenType::LPAREN);
        std::optional<std::string> col_name;
        if (check(TokenType::STAR)) consume();
        else col_name = expect(TokenType::IDENT).value;
        expect(TokenType::RPAREN);
        std::optional<std::string> alias;
        if (match(TokenType::KW_AS)) alias = expect(TokenType::IDENT).value;
        return AggregateCol{func, col_name, alias};
    }

    // regular column reference
    std::string name = expect(TokenType::IDENT).value;
    std::optional<std::string> alias;
    if (match(TokenType::KW_AS)) alias = expect(TokenType::IDENT).value;
    return RegularCol{name, alias};
}

// ── Condition grammar (OR > AND > primary) ───────────────────────────────────

Expr Parser::parse_condition() {
    Expr left = parse_and();
    while (check(TokenType::KW_OR)) {
        consume();
        Expr right = parse_and();
        left = std::make_shared<OrExpr>(OrExpr{std::move(left), std::move(right)});
    }
    return left;
}

Expr Parser::parse_and() {
    Expr left = parse_primary_cond();
    while (check(TokenType::KW_AND)) {
        consume();
        Expr right = parse_primary_cond();
        left = std::make_shared<AndExpr>(AndExpr{std::move(left), std::move(right)});
    }
    return left;
}

Expr Parser::parse_primary_cond() {
    // parenthesised condition
    if (check(TokenType::LPAREN)) {
        consume();
        Expr e = parse_condition();
        expect(TokenType::RPAREN);
        return e;
    }

    Expr left = parse_value_expr();

    // BETWEEN
    if (check(TokenType::KW_BETWEEN)) {
        consume();
        Expr lo = parse_value_expr();
        expect(TokenType::KW_AND);
        Expr hi = parse_value_expr();
        return std::make_shared<BetweenExpr>(BetweenExpr{
            std::move(left), std::move(lo), std::move(hi)});
    }

    // LIKE
    if (check(TokenType::KW_LIKE)) {
        consume();
        Expr pat = parse_value_expr();
        return std::make_shared<LikeExpr>(LikeExpr{std::move(left), std::move(pat)});
    }

    // comparison operators
    CmpOp op;
    bool  is_cmp = false;
    if      (match(TokenType::OP_EQ))  { op = CmpOp::EQ;  is_cmp = true; }
    else if (match(TokenType::OP_NEQ)) { op = CmpOp::NEQ; is_cmp = true; }
    else if (match(TokenType::OP_LE))  { op = CmpOp::LE;  is_cmp = true; }
    else if (match(TokenType::OP_GE))  { op = CmpOp::GE;  is_cmp = true; }
    else if (match(TokenType::OP_LT))  { op = CmpOp::LT;  is_cmp = true; }
    else if (match(TokenType::OP_GT))  { op = CmpOp::GT;  is_cmp = true; }

    if (is_cmp) {
        Expr right = parse_value_expr();
        return std::make_shared<BinCondExpr>(BinCondExpr{
            std::move(left), op, std::move(right)});
    }

    throw ParseError("Expected comparison operator or BETWEEN/LIKE in condition near '" +
                     peek().value + "' line " + std::to_string(peek().line));
}

// ── value expressions ─────────────────────────────────────────────────────────

Expr Parser::parse_value_expr() {
    const Token& t = peek();
    if (t.type == TokenType::KW_NULL) {
        consume(); return NullExpr{};
    }
    if (t.type == TokenType::LIT_INT) {
        consume(); return IntLitExpr{std::stoll(t.value)};
    }
    if (t.type == TokenType::LIT_STRING) {
        consume(); return StrLitExpr{t.value};
    }
    if (t.type == TokenType::IDENT) {
        consume(); return ColRefExpr{t.value};
    }
    throw ParseError("Expected value expression, got '" + t.value +
                     "' at line " + std::to_string(t.line));
}

std::string Parser::parse_table_ref() {
    std::string name = expect(TokenType::IDENT).value;
    if (match(TokenType::DOT))
        name = name + "." + expect(TokenType::IDENT).value;
    return name;
}

} // namespace db
