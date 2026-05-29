#pragma once
#include "schema.h"
#include "string_pool.h"
#include <b_tree.h>
#include <memory>
#include <vector>
#include <optional>
#include <filesystem>
#include <fstream>

namespace db {

class Table; // forward declaration for RowScanner


class RowScanner {
    std::ifstream  _file;
    const Table&   _table;
public:
    explicit RowScanner(const Table& tbl);

    // Returns the next live row, or nullopt at EOF.
    // The returned Row is move-constructed — the caller owns it.
    std::optional<std::pair<int64_t, Row>> next();

    bool eof() const { return !_file.good(); }
};

class IndexBase {
public:
    virtual ~IndexBase() = default;
    virtual void insert(const ColumnValue& key, int64_t row_id) = 0;
    virtual void erase (const ColumnValue& key, int64_t row_id) = 0;
    virtual std::vector<int64_t> find_equal(const ColumnValue& key) const = 0;
    virtual std::vector<int64_t> find_range(const ColumnValue& lo,
                                            const ColumnValue& hi) const = 0;
    virtual void clear() = 0;
    virtual void save(const std::filesystem::path& path) const = 0;
    virtual bool load(const std::filesystem::path& path) = 0; // false = file missing
};


class Table {
    TableSchema              _schema;
    int64_t                  _record_size = 0; // bytes per record (fixed)

    // Primary index in RAM: row_id → byte offset in data file
    B_tree<int64_t, int64_t> _index;

    struct IndexEntry {
        size_t                    col_idx;
        std::unique_ptr<IndexBase> index;
    };
    std::vector<IndexEntry> _indexes; // secondary indexes (RAM only)

    // File paths
    std::filesystem::path _base_path;   // {db_dir}/{table_name}  (no extension)
    std::filesystem::path _data_path;   // .dat
    std::filesystem::path _idx_path;    // .idx
    std::filesystem::path _schema_path; // .schema.json

    bool         _dirty = false;
    StringPool&  _pool;

    void    compute_record_size();
    void    create_secondary_indexes(); // allocate IntIndex/StrIndex objects

    // Row ↔ binary: reads/writes exactly (_record_size - 9) bytes of column data
    void    write_columns(std::ostream& out, const Row& row) const;
    Row     read_columns (std::istream& in)                   const;

    // Append one record to data file; return its byte offset
    int64_t append_record(int64_t row_id, const Row& row);

    // Mark the record at offset as deleted (write valid=0)
    void    mark_deleted(int64_t offset);

    // Read a row from a random offset (seeks to offset, reads valid+id+cols)
    std::optional<Row> read_row_at(int64_t offset) const;

    // Rebuild main index by scanning .dat sequentially (recovery/first load)
    void rebuild_index_from_data();

    // Secondary index maintenance
    void add_to_indexes   (int64_t row_id, const Row& row);
    void remove_from_indexes(int64_t row_id, const Row& row);
    void rebuild_secondary_indexes();

    // Persistence helpers
    void save_schema() const;
    void load_schema();
    void save_main_index() const;
    void load_main_index();
    void save_secondary_indexes() const;
    void load_secondary_indexes();           // loads or rebuilds if files missing
    std::filesystem::path sidx_path(size_t col_idx) const; // path for secondary index

public:
    // base_path = {db_dir}/{table_name}  (no extension)
    Table(TableSchema schema, std::filesystem::path base_path, StringPool& pool);

    const TableSchema& schema() const noexcept { return _schema; }

    // Returns new row_id
    int64_t insert_row(Row row);
    // Returns false if row_id not found
    bool    update_row(int64_t row_id, const Row& new_row);
    bool    erase_row (int64_t row_id);

    // O(log n) lookup via index + one disk seek
    std::optional<Row> find_row(int64_t row_id) const;

    // Sequential scan of data file (efficient for full-table queries)
    std::vector<std::pair<int64_t, Row>> scan_all() const;

    // Index-assisted lookups — return row_ids (then call find_row)
    std::vector<int64_t> index_lookup(size_t col_idx, const ColumnValue& val) const;
    std::vector<int64_t> index_range (size_t col_idx,
                                      const ColumnValue& lo,
                                      const ColumnValue& hi) const;
    bool has_index(size_t col_idx) const noexcept;

    // Flush index and schema to disk (data file already written incrementally)
    void save();
    // Load index and schema from disk
    void load();

    // Remove all files belonging to this table
    void drop_files();

    // Create a lazy streaming scanner (O(1) extra memory per row)
    RowScanner make_scanner() const;

    bool dirty() const noexcept { return _dirty; }
    void mark_dirty()           { _dirty = true;  }

    friend class RowScanner;
};

} // namespace db
