#pragma once
#include "ast.h"
#include "storage_manager.h"
#include <string>
#include <vector>

namespace db {

struct QueryResult {
    bool success = true;
    std::string message;
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
};

class Executor {
    StorageManager& _sm;

public:
    explicit Executor(StorageManager& sm);

    QueryResult execute(const Statement& stmt);

    QueryResult exec_create_db(const CreateDatabaseStmt&);
    QueryResult exec_drop_db(const DropDatabaseStmt&);
    QueryResult exec_use(const UseDatabaseStmt&);
    QueryResult exec_create_tbl(const CreateTableStmt&);
    QueryResult exec_drop_tbl(const DropTableStmt&);
    QueryResult exec_insert(const InsertStmt&);
    QueryResult exec_update(const UpdateStmt&);
    QueryResult exec_delete(const DeleteStmt&);
    QueryResult exec_select(const SelectStmt&);

private:

    std::pair<Database*, Table*> resolve_table(const std::string& ref);

    ColumnValue eval_expr(const Expr& e, const Row& row,
                          const TableSchema& schema) const;

    bool eval_cond(const Expr& e, const Row& row,
                   const TableSchema& schema) const;

    bool compare_values(const ColumnValue& lhs, CmpOp op,
                        const ColumnValue& rhs) const;

    static std::string agg_col_header(const AggregateCol& a);
};

} // namespace db
