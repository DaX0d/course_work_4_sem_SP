#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <optional>

namespace db {

struct ColumnDef {
    std::string name;
    ColumnType  type;
    bool not_null  = false;
    bool indexed   = false;
    std::optional<ColumnValue> default_val; // task 10
};

struct TableSchema {
    std::string name;
    std::vector<ColumnDef> columns;
    int64_t next_id = 1;

    // Returns column index or -1 if not found
    int col_index(const std::string& col_name) const noexcept {
        for (int i = 0; i < static_cast<int>(columns.size()); ++i)
            if (columns[i].name == col_name) return i;
        return -1;
    }
};

} // namespace db
