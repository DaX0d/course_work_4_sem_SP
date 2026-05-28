#pragma once
#include "ast.h"
#include "storage_manager.h"
#include <string>
#include <vector>

namespace db {

struct QueryResult {
    bool                              success = true;
    std::string                       message;
    std::vector<std::string>          headers;
    std::vector<std::vector<std::string>> rows; // stringified cell values
};

class Executor {
    StorageManager& _sm;

public:
    explicit Executor(StorageManager& sm);

    QueryResult execute(const Statement& stmt);

    QueryResult exec_create_db  (const CreateDatabaseStmt&);
    QueryResult exec_drop_db    (const DropDatabaseStmt&);
    QueryResult exec_use        (const UseDatabaseStmt&);
    QueryResult exec_create_tbl (const CreateTableStmt&);
    QueryResult exec_drop_tbl   (const DropTableStmt&);
    QueryResult exec_insert     (const InsertStmt&);
    QueryResult exec_update     (const UpdateStmt&);
    QueryResult exec_delete     (const DeleteStmt&);
    QueryResult exec_select     (const SelectStmt&);

private:

    // Resolve "db.table" or "table" → (Database*, Table*)
    std::pair<Database*, Table*> resolve_table(const std::string& ref);

    // Evaluate a value expression against one row
    ColumnValue eval_expr(const Expr& e, const Row& row,
                          const TableSchema& schema) const;

    // Evaluate a boolean condition (BinCondExpr / AndExpr / OrExpr / …)
    bool eval_cond(const Expr& e, const Row& row,
                   const TableSchema& schema) const;

    // Compare two ColumnValues with a CmpOp; returns false on type mismatch
    bool compare_values(const ColumnValue& lhs, CmpOp op,
                        const ColumnValue& rhs) const;

    static std::string agg_col_header(const AggregateCol& a);
};

} // namespace db
