#include "table.h"
#include "types.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace db {

template<typename T>
static void write_pod(std::ostream& out, T v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template<typename T>
static T read_pod(std::istream& in) {
    T v{};
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return v;
}


class IntIndex final : public IndexBase {
    B_tree<int64_t, std::vector<int64_t>> _tree;
public:
    void insert(const ColumnValue& key, int64_t row_id) override {
        if (!is_int(key)) return;
        int64_t k = get_int(key);
        auto it = _tree.find(k);
        if (it == _tree.end()) _tree.insert({k, {row_id}});
        else                   _tree.at(k).push_back(row_id);
    }
    void erase(const ColumnValue& key, int64_t row_id) override {
        if (!is_int(key)) return;
        int64_t k = get_int(key);
        auto it = _tree.find(k);
        if (it == _tree.end()) return;
        auto& ids = _tree.at(k);
        ids.erase(std::remove(ids.begin(), ids.end(), row_id), ids.end());
        if (ids.empty()) _tree.erase(k);
    }
    std::vector<int64_t> find_equal(const ColumnValue& key) const override {
        if (!is_int(key)) return {};
        auto it = _tree.find(get_int(key));
        if (it == _tree.cend()) return {};
        return it->second;
    }
    std::vector<int64_t> find_range(const ColumnValue& lo,
                                    const ColumnValue& hi) const override {
        if (!is_int(lo) || !is_int(hi)) return {};
        std::vector<int64_t> res;
        auto it = _tree.lower_bound(get_int(lo));
        while (it != _tree.cend() && it->first < get_int(hi))
            for (auto id : it->second) { res.push_back(id); ++it; }
        return res;
    }
    void clear() override { _tree.clear(); }

    static constexpr uint32_t MAGIC = 0x49494458u; // "IIDX"

    void save(const std::filesystem::path& path) const override {
        std::ofstream f(path, std::ios::binary);
        write_pod<uint32_t>(f, MAGIC);
        uint64_t key_count = 0;
        for (auto it = _tree.cbegin(); it != _tree.cend(); ++it) ++key_count;
        write_pod<uint64_t>(f, key_count);
        for (auto it = _tree.cbegin(); it != _tree.cend(); ++it) {
            write_pod<int64_t>(f, it->first);
            write_pod<uint32_t>(f, static_cast<uint32_t>(it->second.size()));
            for (int64_t id : it->second) write_pod<int64_t>(f, id);
        }
    }

    bool load(const std::filesystem::path& path) override {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        if (read_pod<uint32_t>(f) != MAGIC) return false;
        uint64_t key_count = read_pod<uint64_t>(f);
        _tree.clear();
        for (uint64_t i = 0; i < key_count; ++i) {
            int64_t  key      = read_pod<int64_t>(f);
            uint32_t id_count = read_pod<uint32_t>(f);
            std::vector<int64_t> ids(id_count);
            for (auto& id : ids) id = read_pod<int64_t>(f);
            _tree.insert({key, std::move(ids)});
        }
        return true;
    }
};

class StrIndex final : public IndexBase {
    B_tree<std::string, std::vector<int64_t>> _tree;
public:
    void insert(const ColumnValue& key, int64_t row_id) override {
        if (!is_string(key)) return;
        const std::string& k = get_string(key);
        auto it = _tree.find(k);
        if (it == _tree.end()) _tree.insert({k, {row_id}});
        else                   _tree.at(k).push_back(row_id);
    }
    void erase(const ColumnValue& key, int64_t row_id) override {
        if (!is_string(key)) return;
        const std::string& k = get_string(key);
        auto it = _tree.find(k);
        if (it == _tree.end()) return;
        auto& ids = _tree.at(k);
        ids.erase(std::remove(ids.begin(), ids.end(), row_id), ids.end());
        if (ids.empty()) _tree.erase(k);
    }
    std::vector<int64_t> find_equal(const ColumnValue& key) const override {
        if (!is_string(key)) return {};
        auto it = _tree.find(get_string(key));
        if (it == _tree.cend()) return {};
        return it->second;
    }
    std::vector<int64_t> find_range(const ColumnValue& lo,
                                    const ColumnValue& hi) const override {
        if (!is_string(lo) || !is_string(hi)) return {};
        std::vector<int64_t> res;
        auto it = _tree.lower_bound(get_string(lo));
        while (it != _tree.cend() && it->first < get_string(hi))
            for (auto id : it->second) { res.push_back(id); ++it; }
        return res;
    }
    void clear() override { _tree.clear(); }

    static constexpr uint32_t MAGIC = 0x53494458u; // "SIDX"

    void save(const std::filesystem::path& path) const override {
        std::ofstream f(path, std::ios::binary);
        write_pod<uint32_t>(f, MAGIC);
        uint64_t key_count = 0;
        for (auto it = _tree.cbegin(); it != _tree.cend(); ++it) ++key_count;
        write_pod<uint64_t>(f, key_count);
        for (auto it = _tree.cbegin(); it != _tree.cend(); ++it) {
            uint32_t klen = static_cast<uint32_t>(it->first.size());
            write_pod<uint32_t>(f, klen);
            f.write(it->first.data(), klen);
            write_pod<uint32_t>(f, static_cast<uint32_t>(it->second.size()));
            for (int64_t id : it->second) write_pod<int64_t>(f, id);
        }
    }

    bool load(const std::filesystem::path& path) override {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        if (read_pod<uint32_t>(f) != MAGIC) return false;
        uint64_t key_count = read_pod<uint64_t>(f);
        _tree.clear();
        for (uint64_t i = 0; i < key_count; ++i) {
            uint32_t klen = read_pod<uint32_t>(f);
            std::string key(klen, '\0');
            f.read(key.data(), klen);
            uint32_t id_count = read_pod<uint32_t>(f);
            std::vector<int64_t> ids(id_count);
            for (auto& id : ids) id = read_pod<int64_t>(f);
            _tree.insert({std::move(key), std::move(ids)});
        }
        return true;
    }
};


void Table::compute_record_size() {
    _record_size = 1 + 8; // valid + row_id
    for (const auto& col : _schema.columns)
        _record_size += 1 + (col.type == ColumnType::INT ? 8 : 4);
}

void Table::create_secondary_indexes() {
    _indexes.clear();
    for (size_t i = 0; i < _schema.columns.size(); ++i) {
        if (!_schema.columns[i].indexed) continue;
        IndexEntry e;
        e.col_idx = i;
        if (_schema.columns[i].type == ColumnType::INT)
            e.index = std::make_unique<IntIndex>();
        else
            e.index = std::make_unique<StrIndex>();
        _indexes.push_back(std::move(e));
    }
}

void Table::write_columns(std::ostream& out, const Row& row) const {
    for (size_t i = 0; i < _schema.columns.size(); ++i) {
        const ColumnValue& val = (i < row.size()) ? row[i] : ColumnValue{NullValue{}};
        uint8_t null_flag = is_null(val) ? 1 : 0;
        write_pod<uint8_t>(out, null_flag);
        if (_schema.columns[i].type == ColumnType::INT) {
            int64_t v = (!null_flag && is_int(val)) ? get_int(val) : 0LL;
            write_pod<int64_t>(out, v);
        } else {
            uint32_t pid = 0;
            if (!null_flag && is_string(val))
                pid = _pool.intern(get_string(val));
            write_pod<uint32_t>(out, pid);
        }
    }
}

Row Table::read_columns(std::istream& in) const {
    Row row;
    row.reserve(_schema.columns.size());
    for (const auto& col : _schema.columns) {
        uint8_t null_flag = read_pod<uint8_t>(in);
        if (col.type == ColumnType::INT) {
            int64_t v = read_pod<int64_t>(in);
            row.push_back(null_flag ? ColumnValue{NullValue{}} : ColumnValue{v});
        } else {
            uint32_t pid = read_pod<uint32_t>(in);
            if (null_flag) row.push_back(NullValue{});
            else           row.push_back(ColumnValue{_pool.resolve(pid)});
        }
    }
    return row;
}

int64_t Table::append_record(int64_t row_id, const Row& row) {
    std::ofstream f(_data_path, std::ios::binary | std::ios::app);
    if (!f) throw DbError("Table::append_record: cannot open " + _data_path.string());

    int64_t offset = static_cast<int64_t>(std::filesystem::file_size(_data_path));
    write_pod<uint8_t>(f, 1);         // valid = 1
    write_pod<int64_t>(f, row_id);
    write_columns(f, row);
    return offset;
}

void Table::mark_deleted(int64_t offset) {
    // Open for read+write without truncating
    std::fstream f(_data_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) return;
    f.seekp(offset);
    write_pod<uint8_t>(f, 0); // valid = 0
}

RowScanner::RowScanner(const Table& tbl)
    : _file(tbl._data_path, std::ios::binary)
    , _table(tbl)
{}

std::optional<std::pair<int64_t, Row>> RowScanner::next() {
    while (_file.good()) {
        uint8_t valid = read_pod<uint8_t>(_file);
        if (!_file) return std::nullopt;              // EOF
        int64_t row_id = read_pod<int64_t>(_file);
        Row row = _table.read_columns(_file);         // always consume full record
        if (valid == 1)
            return std::make_pair(row_id, std::move(row));
        // deleted record: columns already consumed, try next
    }
    return std::nullopt;
}

RowScanner Table::make_scanner() const {
    return RowScanner(*this);
}

std::optional<Row> Table::read_row_at(int64_t offset) const {
    std::ifstream f(_data_path, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(offset);
    uint8_t valid = read_pod<uint8_t>(f);
    if (!valid) return std::nullopt;
    read_pod<int64_t>(f); // skip stored row_id
    return read_columns(f);
}

Table::Table(TableSchema schema, std::filesystem::path base_path, StringPool& pool)
    : _schema(std::move(schema))
    , _base_path  (base_path)
    , _data_path  (base_path.string() + ".dat")
    , _idx_path   (base_path.string() + ".idx")
    , _schema_path(base_path.string() + ".schema.json")
    , _pool(pool)
{
    compute_record_size();
    create_secondary_indexes();

    if (std::filesystem::exists(_data_path)) {
        load();
    } else {
        // Brand-new table: create empty data file so we can append later
        std::ofstream f(_data_path, std::ios::binary);
    }
}

void Table::add_to_indexes(int64_t row_id, const Row& row) {
    for (auto& e : _indexes)
        if (e.col_idx < row.size())
            e.index->insert(row[e.col_idx], row_id);
}

void Table::remove_from_indexes(int64_t row_id, const Row& row) {
    for (auto& e : _indexes)
        if (e.col_idx < row.size())
            e.index->erase(row[e.col_idx], row_id);
}

void Table::rebuild_secondary_indexes() {
    for (auto& e : _indexes) e.index->clear();
    // Stream rows one-by-one — O(1) extra RAM regardless of table size
    auto sc = make_scanner();
    while (auto entry = sc.next())
        add_to_indexes(entry->first, entry->second);
}

int64_t Table::insert_row(Row row) {
    int64_t id     = _schema.next_id++;
    int64_t offset = append_record(id, row);
    _index.insert({id, offset});
    add_to_indexes(id, row);
    _dirty = true;
    return id;
}

bool Table::update_row(int64_t row_id, const Row& new_row) {
    auto it = _index.find(row_id);
    if (it == _index.end()) return false;
    int64_t old_offset = it->second;

    // Remove old row from secondary indexes
    auto old_row = read_row_at(old_offset);
    if (old_row) remove_from_indexes(row_id, *old_row);

    // Soft-delete old record, append new one
    mark_deleted(old_offset);
    int64_t new_offset = append_record(row_id, new_row);

    // Update primary index
    _index.insert_or_assign({row_id, new_offset});

    add_to_indexes(row_id, new_row);
    _dirty = true;
    return true;
}

bool Table::erase_row(int64_t row_id) {
    auto it = _index.find(row_id);
    if (it == _index.end()) return false;
    int64_t offset = it->second;

    auto old_row = read_row_at(offset);
    if (old_row) remove_from_indexes(row_id, *old_row);

    mark_deleted(offset);
    _index.erase(row_id);
    _dirty = true;
    return true;
}

std::optional<Row> Table::find_row(int64_t row_id) const {
    auto it = _index.find(row_id);
    if (it == _index.cend()) return std::nullopt;
    return read_row_at(it->second);
}

// Sequential scan — efficient for full-table queries
std::vector<std::pair<int64_t, Row>> Table::scan_all() const {
    std::vector<std::pair<int64_t, Row>> result;
    std::ifstream f(_data_path, std::ios::binary);
    if (!f) return result;

    while (true) {
        uint8_t valid = read_pod<uint8_t>(f);
        if (!f) break;
        int64_t row_id = read_pod<int64_t>(f);
        Row row = read_columns(f);
        if (valid == 1) result.push_back({row_id, std::move(row)});
    }
    return result;
}

std::vector<int64_t> Table::index_lookup(size_t col_idx, const ColumnValue& val) const {
    for (const auto& e : _indexes)
        if (e.col_idx == col_idx) return e.index->find_equal(val);
    return {};
}

std::vector<int64_t> Table::index_range(size_t col_idx,
                                         const ColumnValue& lo,
                                         const ColumnValue& hi) const {
    for (const auto& e : _indexes)
        if (e.col_idx == col_idx) return e.index->find_range(lo, hi);
    return {};
}

bool Table::has_index(size_t col_idx) const noexcept {
    for (const auto& e : _indexes)
        if (e.col_idx == col_idx) return true;
    return false;
}

static constexpr uint32_t IDX_MAGIC = 0x42494458u; // "BIDX"

void Table::save_main_index() const {
    std::ofstream f(_idx_path, std::ios::binary);
    if (!f) throw DbError("Table::save_main_index: cannot open " + _idx_path.string());
    write_pod<uint32_t>(f, IDX_MAGIC);
    write_pod<uint64_t>(f, static_cast<uint64_t>(_index.size()));
    write_pod<int64_t> (f, _schema.next_id);
    for (auto it = _index.cbegin(); it != _index.cend(); ++it) {
        write_pod<int64_t>(f, it->first);   // row_id
        write_pod<int64_t>(f, it->second);  // offset
    }
}

void Table::load_main_index() {
    std::ifstream f(_idx_path, std::ios::binary);
    if (!f) return;
    uint32_t magic = read_pod<uint32_t>(f);
    if (magic != IDX_MAGIC) return; // corrupt/unknown; will rebuild below
    uint64_t count    = read_pod<uint64_t>(f);
    int64_t  next_id  = read_pod<int64_t>(f);
    _schema.next_id = next_id;
    _index.clear();
    for (uint64_t i = 0; i < count; ++i) {
        int64_t key = read_pod<int64_t>(f);
        int64_t val = read_pod<int64_t>(f);
        _index.insert({key, val});
    }
}

void Table::rebuild_index_from_data() {
    // Fallback: scan .dat sequentially and rebuild the primary index
    _index.clear();
    std::ifstream f(_data_path, std::ios::binary);
    if (!f) return;

    int64_t offset = 0;
    while (true) {
        uint8_t valid = read_pod<uint8_t>(f);
        if (!f) break;
        int64_t row_id = read_pod<int64_t>(f);
        // Skip column bytes without decoding (just advance the stream)
        f.seekg(offset + _record_size);
        if (!f) break;
        if (valid == 1)
            _index.insert({row_id, offset});
        offset += _record_size;
    }
}

void Table::save_schema() const {
    nlohmann::json j;
    j["name"]    = _schema.name;
    j["next_id"] = _schema.next_id;
    nlohmann::json cols = nlohmann::json::array();
    for (const auto& c : _schema.columns) {
        nlohmann::json cj;
        cj["name"]     = c.name;
        cj["type"]     = (c.type == ColumnType::INT) ? "int" : "string";
        cj["not_null"] = c.not_null;
        cj["indexed"]  = c.indexed;
        if (c.default_val) {
            if (is_null(*c.default_val))        cj["default"] = nullptr;
            else if (is_int(*c.default_val))    cj["default"] = get_int(*c.default_val);
            else                                cj["default"] = get_string(*c.default_val);
        } else {
            cj["default"] = nullptr;
        }
        cols.push_back(std::move(cj));
    }
    j["columns"] = std::move(cols);
    std::ofstream f(_schema_path);
    if (!f) throw DbError("Table::save_schema: " + _schema_path.string());
    f << j.dump(2);
}

void Table::load_schema() {
    std::ifstream f(_schema_path);
    if (!f) return;
    nlohmann::json j;
    f >> j;
    _schema.name    = j["name"];
    _schema.next_id = j["next_id"];
    _schema.columns.clear();
    for (const auto& cj : j["columns"]) {
        ColumnDef cd;
        cd.name     = cj["name"];
        cd.type     = (cj["type"] == "int") ? ColumnType::INT : ColumnType::STRING;
        cd.not_null = cj["not_null"];
        cd.indexed  = cj["indexed"];
        if (!cj["default"].is_null()) {
            if (cd.type == ColumnType::INT)
                cd.default_val = static_cast<int64_t>(cj["default"].get<int64_t>());
            else
                cd.default_val = cj["default"].get<std::string>();
        }
        _schema.columns.push_back(std::move(cd));
    }
    compute_record_size();
    create_secondary_indexes();
}

std::filesystem::path Table::sidx_path(size_t col_idx) const {
    return _base_path.string() + "_" + _schema.columns[col_idx].name + ".sidx";
}

void Table::save_secondary_indexes() const {
    for (const auto& e : _indexes)
        e.index->save(sidx_path(e.col_idx));
}

void Table::load_secondary_indexes() {
    bool all_loaded = true;
    for (auto& e : _indexes) {
        if (!e.index->load(sidx_path(e.col_idx))) {
            all_loaded = false;
            break;
        }
    }
    if (!all_loaded) {
        // At least one .sidx is missing — rebuild everything from .dat
        for (auto& e : _indexes) e.index->clear();
        auto sc = make_scanner();
        while (auto entry = sc.next())
            add_to_indexes(entry->first, entry->second);
        // Persist so we don't rebuild next time
        save_secondary_indexes();
    }
}

void Table::save() {
    save_schema();
    save_main_index();
    save_secondary_indexes();
    if (_pool.dirty()) _pool.save();
    _dirty = false;
}

void Table::load() {
    if (std::filesystem::exists(_schema_path)) load_schema();

    if (std::filesystem::exists(_idx_path)) {
        load_main_index();
    } else {
        rebuild_index_from_data();
    }

    // Load secondary indexes from .sidx files; rebuild from .dat if any are missing
    load_secondary_indexes();
}

void Table::drop_files() {
    std::filesystem::remove(_data_path);
    std::filesystem::remove(_idx_path);
    std::filesystem::remove(_schema_path);
    for (const auto& e : _indexes)
        std::filesystem::remove(sidx_path(e.col_idx));
}

std::string column_value_to_display(const ColumnValue& v) {
    if (is_null(v))   return "NULL";
    if (is_int(v))    return std::to_string(get_int(v));
    return get_string(v);
}

} // namespace db
