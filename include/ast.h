#pragma once
#include "types.h"
#include "schema.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <variant>

namespace db {

// ─────────────────────────────────────────────────────────────────────────────
// Value expressions (right-hand side of comparisons / assignment values)
// ─────────────────────────────────────────────────────────────────────────────

struct NullExpr   { };
struct IntLitExpr { int64_t value; };
struct StrLitExpr { std::string value; };
struct ColRefExpr { std::string name; };   // reference to a column

// Forward declarations for recursive nodes
struct BinCondExpr;
struct BetweenExpr;
struct LikeExpr;
struct AndExpr;
struct OrExpr;

// An expression evaluates to a ColumnValue at runtime
using Expr = std::variant<
    NullExpr,
    IntLitExpr,
    StrLitExpr,
    ColRefExpr,
    std::shared_ptr<BinCondExpr>,
    std::shared_ptr<BetweenExpr>,
    std::shared_ptr<LikeExpr>,
    std::shared_ptr<AndExpr>,
    std::shared_ptr<OrExpr>
>;

enum class CmpOp { EQ, NEQ, LT, GT, LE, GE };

// col OP value  (or value OP value)
struct BinCondExpr { Expr left; CmpOp op; Expr right; };
// val BETWEEN lo AND hi  →  lo <= val < hi
struct BetweenExpr { Expr value; Expr lo; Expr hi; };
// val LIKE pattern
struct LikeExpr    { Expr value; Expr pattern; };
// condition AND condition
struct AndExpr     { Expr left; Expr right; };
// condition OR  condition
struct OrExpr      { Expr left; Expr right; };

// ─────────────────────────────────────────────────────────────────────────────
// SELECT column descriptors
// ─────────────────────────────────────────────────────────────────────────────

struct StarCol  { };
struct RegularCol   { std::string name; std::optional<std::string> alias; };

enum class AggFunc { SUM, COUNT, AVG };
struct AggregateCol {
    AggFunc func;
    std::optional<std::string> col_name; // nullopt for COUNT(*)
    std::optional<std::string> alias;
};

using SelectCol = std::variant<StarCol, RegularCol, AggregateCol>;

// ─────────────────────────────────────────────────────────────────────────────
// Column definition inside CREATE TABLE
// ─────────────────────────────────────────────────────────────────────────────

struct ColDefNode {
    std::string                name;
    ColumnType                 type;
    bool                       not_null    = false;
    bool                       indexed     = false;
    std::optional<ColumnValue> default_val; // task 10
};

// ─────────────────────────────────────────────────────────────────────────────
// Statement nodes
// ─────────────────────────────────────────────────────────────────────────────

struct CreateDatabaseStmt { std::string name; };
struct DropDatabaseStmt   { std::string name; };
struct UseDatabaseStmt    { std::string name; };

struct CreateTableStmt {
    std::string             table_name;
    std::vector<ColDefNode> columns;
};

struct DropTableStmt { std::string table_name; };

struct InsertStmt {
    std::string                           table_name;
    std::vector<std::string>              columns;
    std::vector<std::vector<Expr>>        values; // multiple rows
};

struct UpdateStmt {
    std::string                                  table_name;
    std::vector<std::pair<std::string, Expr>>    assignments;
    std::optional<Expr>                          where;
};

struct DeleteStmt {
    std::string         table_name;
    std::optional<Expr> where;
};

struct SelectStmt {
    std::string              table_name;
    std::vector<SelectCol>   columns;
    std::optional<Expr>      where;
};

using Statement = std::variant<
    CreateDatabaseStmt, DropDatabaseStmt, UseDatabaseStmt,
    CreateTableStmt,    DropTableStmt,
    InsertStmt, UpdateStmt, DeleteStmt, SelectStmt
>;

} // namespace db
