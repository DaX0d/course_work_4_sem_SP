#include "executor.h"
#include "types.h"
#include <regex>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>

namespace db {

Executor::Executor(StorageManager& sm) : _sm(sm) {}


QueryResult Executor::execute(const Statement& stmt) {
    return std::visit([this](const auto& s) -> QueryResult {
        using T = std::decay_t<decltype(s)>;
        if      constexpr (std::is_same_v<T, CreateDatabaseStmt>) return exec_create_db(s);
        else if constexpr (std::is_same_v<T, DropDatabaseStmt>) return exec_drop_db(s);
        else if constexpr (std::is_same_v<T, UseDatabaseStmt>) return exec_use(s);
        else if constexpr (std::is_same_v<T, CreateTableStmt>) return exec_create_tbl(s);
        else if constexpr (std::is_same_v<T, DropTableStmt>) return exec_drop_tbl(s);
        else if constexpr (std::is_same_v<T, InsertStmt>) return exec_insert(s);
        else if constexpr (std::is_same_v<T, UpdateStmt>) return exec_update(s);
        else if constexpr (std::is_same_v<T, DeleteStmt>) return exec_delete(s);
        else if constexpr (std::is_same_v<T, SelectStmt>) return exec_select(s);
        else return QueryResult{false, "Unknown statement type"};
    }, stmt);
}

QueryResult Executor::exec_create_db(const CreateDatabaseStmt& s) {
    try {
        _sm.create_database(s.name);
        return {true, "Database created: " + s.name};
    } catch (const DbError& e) {
        return {false, e.what()};
    }
}

QueryResult Executor::exec_drop_db(const DropDatabaseStmt& s) {
    try {
        _sm.drop_database(s.name);
        return {true, "Database dropped: " + s.name};
    } catch (const DbError& e) {
        return {false, e.what()};
    }
}

QueryResult Executor::exec_use(const UseDatabaseStmt& s) {
    try {
        _sm.use_database(s.name);
        _sm.current_db()->access_log().log(s.name, "-", "USE", true);
        return {true, "Using database: " + s.name};
    } catch (const DbError& e) {
        return {false, e.what()};
    }
}

QueryResult Executor::exec_create_tbl(const CreateTableStmt& s) {
    try {
        Database* db = _sm.current_db();
        TableSchema schema;
        schema.name = s.table_name;
        for (const auto& cd : s.columns) {
            ColumnDef def;
            def.name = cd.name;
            def.type = cd.type;
            def.not_null = cd.not_null;
            def.indexed = cd.indexed;
            def.default_val = cd.default_val;
            schema.columns.push_back(std::move(def));
        }
        db->create_table(std::move(schema));
        db->access_log().log(_sm.current_db_name(), s.table_name, "CREATE TABLE", true);
        return {true, "Table created: " + s.table_name};
    } catch (const DbError& e) {
        return {false, e.what()};
    }
}

QueryResult Executor::exec_drop_tbl(const DropTableStmt& s) {
    try {
        Database* db = _sm.current_db();
        db->drop_table(s.table_name);
        db->access_log().log(_sm.current_db_name(), s.table_name, "DROP TABLE", true);
        return {true, "Table dropped: " + s.table_name};
    } catch (const DbError& e) {
        return {false, e.what()};
    }
}

QueryResult Executor::exec_insert(const InsertStmt& s) {
    try {
        auto [db, tbl] = resolve_table(s.table_name);
        const TableSchema& schema = tbl->schema();

        // Build column index mapping: insert col → schema index
        std::vector<int> col_map;
        for (const auto& cname : s.columns) {
            int idx = schema.col_index(cname);
            if (idx < 0)
                throw DbError("Unknown column: " + cname);
            col_map.push_back(idx);
        }

        int inserted = 0;
        for (const auto& val_row : s.values) {
            if (val_row.size() != s.columns.size())
                throw DbError("Column count mismatch in INSERT");

            Row row(schema.columns.size(), NullValue{});

            // Apply column values from INSERT
            for (size_t i = 0; i < val_row.size(); ++i) {
                ColumnValue cv = eval_expr(val_row[i], {}, schema);
                int si = col_map[i];
                // type check
                const ColumnDef& cd = schema.columns[si];
                if (!is_null(cv) && cd.type == ColumnType::INT && !is_int(cv))
                    throw DbError("Type mismatch for column " + cd.name + ": expected INT");
                if (!is_null(cv) && cd.type == ColumnType::STRING && !is_string(cv))
                    throw DbError("Type mismatch for column " + cd.name + ": expected STRING");
                row[si] = std::move(cv);
            }

            // Apply defaults for unspecified columns (task 10)
            for (size_t ci = 0; ci < schema.columns.size(); ++ci) {
                if (!is_null(row[ci])) continue;
                const ColumnDef& cd = schema.columns[ci];
                if (cd.default_val) {
                    row[ci] = *cd.default_val;
                } else if (cd.not_null) {
                    throw DbError("NOT_NULL constraint violated for column: " + cd.name);
                }
            }

            tbl->insert_row(std::move(row));
            ++inserted;
        }

        tbl->save();
        db->access_log().log(_sm.current_db_name(), s.table_name, "INSERT", true);
        return {true, "Inserted " + std::to_string(inserted) + " row(s)"};
    } catch (const DbError& e) {
        if (_sm.has_current_db())
            _sm.current_db()->access_log().log(
                _sm.current_db_name(), s.table_name, "INSERT", false);
        return {false, e.what()};
    }
}

QueryResult Executor::exec_update(const UpdateStmt& s) {
    try {
        auto [db, tbl] = resolve_table(s.table_name);
        const TableSchema& schema = tbl->schema();

        // Validate assignment columns
        std::vector<std::pair<int, Expr>> assignments;
        for (const auto& [cname, expr] : s.assignments) {
            int idx = schema.col_index(cname);
            if (idx < 0) throw DbError("Unknown column: " + cname);
            assignments.push_back({idx, expr});
        }

        //  Pass 1: stream through the file, collect only MATCHING (id, new_row)
        // Memory used: O(matched), not O(total).
        std::vector<std::pair<int64_t, Row>> updates;
        {
            auto sc = tbl->make_scanner();
            while (auto entry = sc.next()) {
                auto& [row_id, row] = *entry;
                if (s.where && !eval_cond(*s.where, row, schema)) continue;
                Row new_row = row;
                for (auto& [ci, expr] : assignments) {
                    ColumnValue cv = eval_expr(expr, row, schema);
                    const ColumnDef& cd = schema.columns[ci];
                    if (cd.not_null && is_null(cv))
                        throw DbError("NOT_NULL constraint violated for: " + cd.name);
                    new_row[ci] = std::move(cv);
                }
                updates.push_back({row_id, std::move(new_row)});
            }
        } // scanner + its file handle are destroyed here

        // Pass 2: apply updates (file re-opened per update_row call)
        for (auto& [rid, nr] : updates) tbl->update_row(rid, nr);

        tbl->save();
        db->access_log().log(_sm.current_db_name(), s.table_name, "UPDATE", true);
        return {true, "Updated " + std::to_string(updates.size()) + " row(s)"};
    } catch (const DbError& e) {
        if (_sm.has_current_db())
            _sm.current_db()->access_log().log(
                _sm.current_db_name(), s.table_name, "UPDATE", false);
        return {false, e.what()};
    }
}

QueryResult Executor::exec_delete(const DeleteStmt& s) {
    try {
        auto [db, tbl] = resolve_table(s.table_name);
        const TableSchema& schema = tbl->schema();

        // ── Pass 1: stream through file, collect only matching row_ids.
        // Memory used: O(matched) × 8 bytes — no row data stored.
        std::vector<int64_t> to_delete;
        {
            auto sc = tbl->make_scanner();
            while (auto entry = sc.next()) {
                if (!s.where || eval_cond(*s.where, entry->second, schema))
                    to_delete.push_back(entry->first);
                // entry goes out of scope here — row data freed immediately
            }
        } // scanner destroyed, file closed

        // ── Pass 2: erase (each call does one index lookup + one seek)
        for (int64_t rid : to_delete) tbl->erase_row(rid);

        tbl->save();
        db->access_log().log(_sm.current_db_name(), s.table_name, "DELETE", true);
        return {true, "Deleted " + std::to_string(to_delete.size()) + " row(s)"};
    } catch (const DbError& e) {
        if (_sm.has_current_db())
            _sm.current_db()->access_log().log(
                _sm.current_db_name(), s.table_name, "DELETE", false);
        return {false, e.what()};
    }
}

QueryResult Executor::exec_select(const SelectStmt& s) {
    try {
        auto [db, tbl] = resolve_table(s.table_name);
        const TableSchema& schema = tbl->schema();

        // Determine if any aggregate function is requested
        bool has_agg = false;
        for (const auto& sc : s.columns)
            if (std::holds_alternative<AggregateCol>(sc)) { has_agg = true; break; }

        //  Aggregate path: O(1) memory only running totals kept in RAM.
        // The file is read once; no row is ever stored.
        if (has_agg) {
            // Accumulators per column descriptor
            struct AggState { int64_t sum = 0; int64_t cnt = 0; int64_t total = 0; };
            std::vector<AggState> states(s.columns.size());

            auto sc = tbl->make_scanner();
            while (auto entry = sc.next()) {
                auto& [row_id, row] = *entry;
                if (s.where && !eval_cond(*s.where, row, schema)) continue;

                for (size_t i = 0; i < s.columns.size(); ++i) {
                    std::visit([&](const auto& col) {
                        using T = std::decay_t<decltype(col)>;
                        if constexpr (std::is_same_v<T, AggregateCol>) {
                            ++states[i].total;
                            int ci = col.col_name ? schema.col_index(*col.col_name) : -1;
                            bool valid_cell = ci >= 0 && ci < (int)row.size() && !is_null(row[ci]);
                            if (col.func == AggFunc::COUNT) {
                                if (!col.col_name) ++states[i].cnt;         // COUNT(*)
                                else if (valid_cell) ++states[i].cnt;       // COUNT(col)
                            } else if (valid_cell && is_int(row[ci])) {
                                states[i].sum += get_int(row[ci]);
                                ++states[i].cnt;
                            }
                        } else if constexpr (std::is_same_v<T, StarCol>) {
                            ++states[i].total; // COUNT(*) via StarCol
                        }
                    }, s.columns[i]);
                }
            } // scanner destroyed here

            // Build single-row result from accumulators
            QueryResult res;
            for (size_t i = 0; i < s.columns.size(); ++i) {
                std::visit([&](const auto& col) {
                    using T = std::decay_t<decltype(col)>;
                    if constexpr (std::is_same_v<T, StarCol>) {
                        res.headers.push_back("COUNT(*)");
                        res.rows.resize(1);
                        res.rows[0].push_back(std::to_string(states[i].total));
                    } else if constexpr (std::is_same_v<T, RegularCol>) {
                        res.headers.push_back(col.alias.value_or(col.name));
                        res.rows.resize(1);
                        res.rows[0].push_back("NULL");
                    } else if constexpr (std::is_same_v<T, AggregateCol>) {
                        res.headers.push_back(agg_col_header(col));
                        res.rows.resize(1);
                        switch (col.func) {
                            case AggFunc::COUNT:
                                res.rows[0].push_back(std::to_string(states[i].cnt));
                                break;
                            case AggFunc::SUM:
                                res.rows[0].push_back(std::to_string(states[i].sum));
                                break;
                            case AggFunc::AVG:
                                if (states[i].cnt == 0) {
                                    res.rows[0].push_back("NULL");
                                } else {
                                    std::ostringstream oss;
                                    oss << std::fixed << std::setprecision(2)
                                        << (static_cast<double>(states[i].sum) / states[i].cnt);
                                    res.rows[0].push_back(oss.str());
                                }
                                break;
                        }
                    }
                }, s.columns[i]);
            }
            db->access_log().log(_sm.current_db_name(), s.table_name, "SELECT", true);
            return res;
        }

        // Normal path: stream rows, keep only matching ones.
        // Memory used: O(matched), unmatched rows never enter RAM.
        bool has_star = !s.columns.empty() &&
                        std::holds_alternative<StarCol>(s.columns[0]);

        std::vector<int> col_indices;
        std::vector<std::string> headers;
        if (has_star) {
            for (size_t i = 0; i < schema.columns.size(); ++i) {
                col_indices.push_back(static_cast<int>(i));
                headers.push_back(schema.columns[i].name);
            }
        } else {
            for (const auto& sc : s.columns) {
                const auto* rc = std::get_if<RegularCol>(&sc);
                if (!rc) continue;
                int ci = schema.col_index(rc->name);
                if (ci < 0) throw DbError("Unknown column: " + rc->name);
                col_indices.push_back(ci);
                headers.push_back(rc->alias.value_or(rc->name));
            }
        }

        QueryResult res;
        res.headers = headers;
        {
            auto sc = tbl->make_scanner();
            while (auto entry = sc.next()) {
                auto& [row_id, row] = *entry;
                if (s.where && !eval_cond(*s.where, row, schema)) continue;
                // Project only requested columns
                std::vector<std::string> out_row;
                out_row.reserve(col_indices.size());
                for (int ci : col_indices)
                    out_row.push_back(ci < (int)row.size()
                                      ? column_value_to_display(row[ci]) : "NULL");
                res.rows.push_back(std::move(out_row));
            }
        } // scanner destroyed here

        db->access_log().log(_sm.current_db_name(), s.table_name, "SELECT", true);
        return res;
    } catch (const DbError& e) {
        if (_sm.has_current_db())
            _sm.current_db()->access_log().log(
                _sm.current_db_name(), s.table_name, "SELECT", false);
        return {false, e.what()};
    }
}

std::pair<Database*, Table*> Executor::resolve_table(const std::string& ref) {
    std::string db_name, tbl_name;
    auto dot = ref.find('.');
    if (dot != std::string::npos) {
        db_name  = ref.substr(0, dot);
        tbl_name = ref.substr(dot + 1);
    } else {
        tbl_name = ref;
    }

    Database* db = db_name.empty() ? _sm.current_db()
                                   : _sm.get_database(db_name);
    Table* tbl = db->get_table(tbl_name);
    return {db, tbl};
}

ColumnValue Executor::eval_expr(const Expr& e, const Row& row,
                                const TableSchema& schema) const {
    return std::visit([&](const auto& node) -> ColumnValue {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NullExpr>) return NullValue{};
        if constexpr (std::is_same_v<T, IntLitExpr>) return node.value;
        if constexpr (std::is_same_v<T, StrLitExpr>) return node.value;
        if constexpr (std::is_same_v<T, ColRefExpr>) {
            int ci = schema.col_index(node.name);
            if (ci < 0 || ci >= (int)row.size()) return NullValue{};
            return row[ci];
        }
        // Boolean expressions evaluated here return NullValue (they're not value exprs)
        return NullValue{};
    }, e);
}

bool Executor::eval_cond(const Expr& e, const Row& row,
                         const TableSchema& schema) const {
    return std::visit([&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, std::shared_ptr<BinCondExpr>>) {
            ColumnValue lv = eval_expr(node->left,  row, schema);
            ColumnValue rv = eval_expr(node->right, row, schema);
            return compare_values(lv, node->op, rv);
        }
        if constexpr (std::is_same_v<T, std::shared_ptr<AndExpr>>) {
            return eval_cond(node->left, row, schema) &&
                   eval_cond(node->right, row, schema);
        }
        if constexpr (std::is_same_v<T, std::shared_ptr<OrExpr>>) {
            return eval_cond(node->left, row, schema) ||
                   eval_cond(node->right, row, schema);
        }
        if constexpr (std::is_same_v<T, std::shared_ptr<BetweenExpr>>) {
            ColumnValue v  = eval_expr(node->value, row, schema);
            ColumnValue lo = eval_expr(node->lo,    row, schema);
            ColumnValue hi = eval_expr(node->hi,    row, schema);
            return compare_values(lo, CmpOp::LE, v) &&
                   compare_values(v,  CmpOp::LT, hi);
        }
        if constexpr (std::is_same_v<T, std::shared_ptr<LikeExpr>>) {
            ColumnValue v   = eval_expr(node->value,   row, schema);
            ColumnValue pat = eval_expr(node->pattern, row, schema);
            if (!is_string(v) || !is_string(pat)) return false;
            try {
                std::regex re(get_string(pat),
                              std::regex_constants::ECMAScript);
                return std::regex_search(get_string(v), re);
            } catch (...) { return false; }
        }
        return false;
    }, e);
}

bool Executor::compare_values(const ColumnValue& lhs, CmpOp op,
                              const ColumnValue& rhs) const {
    // NULL comparisons always false (SQL semantics)
    if (is_null(lhs) || is_null(rhs)) return false;

    // Both int
    if (is_int(lhs) && is_int(rhs)) {
        int64_t l = get_int(lhs), r = get_int(rhs);
        switch (op) {
            case CmpOp::EQ: return l == r;
            case CmpOp::NEQ: return l != r;
            case CmpOp::LT: return l <  r;
            case CmpOp::GT: return l >  r;
            case CmpOp::LE: return l <= r;
            case CmpOp::GE: return l >= r;
        }
    }
    // Both string
    if (is_string(lhs) && is_string(rhs)) {
        const std::string& l = get_string(lhs);
        const std::string& r = get_string(rhs);
        switch (op) {
            case CmpOp::EQ: return l == r;
            case CmpOp::NEQ: return l != r;
            case CmpOp::LT: return l <  r;
            case CmpOp::GT: return l >  r;
            case CmpOp::LE: return l <= r;
            case CmpOp::GE: return l >= r;
        }
    }
    return false;
}

std::string Executor::agg_col_header(const AggregateCol& a) {
    std::string fn;
    switch (a.func) {
        case AggFunc::SUM: fn = "SUM";   break;
        case AggFunc::COUNT: fn = "COUNT"; break;
        case AggFunc::AVG: fn = "AVG";   break;
    }
    std::string inner = a.col_name.value_or("*");
    std::string base = fn + "(" + inner + ")";
    return a.alias.value_or(base);
}


} // namespace db
