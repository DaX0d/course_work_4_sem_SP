#pragma once
#include "schema.h"
#include "string_pool.h"
#include <b_tree.h>
#include <memory>
#include <vector>
#include <optional>
#include <filesystem>
#include <functional>

namespace db {

// Abstract polymorphic secondary index (one per INDEXED column)
class IndexBase {
public:
    virtual ~IndexBase() = default;
    virtual void insert(const ColumnValue& key, int64_t row_id) = 0;
    virtual void erase (const ColumnValue& key, int64_t row_id) = 0;
    virtual std::vector<int64_t> find_equal(const ColumnValue& key) const = 0;
    virtual std::vector<int64_t> find_range(const ColumnValue& lo,
                                            const ColumnValue& hi) const = 0;
    virtual void clear() = 0;
};

// Main table: B_tree<row_id, Row> persisted as JSON on disk (task: disk storage).
// String values are deduplicated via StringPool (task 2).
class Table {
    TableSchema _schema;
    B_tree<int64_t, Row> _data;   // primary storage

    struct IndexEntry {
        size_t col_idx;
        std::unique_ptr<IndexBase> index;
    };
    std::vector<IndexEntry> _indexes;

    std::filesystem::path _path;
    bool _dirty = false;
    StringPool& _pool;  // task 2

    void rebuild_indexes();
    void add_to_indexes(int64_t row_id, const Row& row);
    void remove_from_indexes(int64_t row_id, const Row& row);

public:
    Table(TableSchema schema, std::filesystem::path path, StringPool& pool);

    const TableSchema& schema() const noexcept { return _schema; }

    // Returns new row_id
    int64_t insert_row(Row row);
    // Returns false if row_id not found
    bool update_row(int64_t row_id, const Row& new_row);
    bool erase_row(int64_t row_id);

    std::optional<Row> find_row(int64_t row_id) const;

    // Full sequential scan
    std::vector<std::pair<int64_t, Row>> scan_all() const;

    // Index-assisted lookup (equality)
    std::vector<int64_t> index_lookup(size_t col_idx, const ColumnValue& val) const;
    // Index-assisted range [lo, hi)
    std::vector<int64_t> index_range(size_t col_idx,
                                      const ColumnValue& lo,
                                      const ColumnValue& hi) const;
    bool has_index(size_t col_idx) const noexcept;

    // Flush B-tree contents to JSON file on disk
    void save();
    // Load JSON file from disk into B-tree
    void load();

    bool dirty() const noexcept { return _dirty; }
    void mark_dirty() { _dirty = true; }
};

} // namespace db
