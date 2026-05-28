#include "table.h"
#include "types.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <stdexcept>

namespace db {

// ─────────────────────────────────────────────────────────────────────────────
// Concrete index implementations (private to this translation unit)
// ─────────────────────────────────────────────────────────────────────────────

class IntIndex final : public IndexBase {
    B_tree<int64_t, std::vector<int64_t>> _tree;

public:
    void insert(const ColumnValue& key, int64_t row_id) override {
        if (!is_int(key)) return;
        int64_t k = get_int(key);
        auto it = _tree.find(k);
        if (it == _tree.end()) {
            _tree.insert({k, {row_id}});
        } else {
            _tree.at(k).push_back(row_id);
        }
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
        if (it == _tree.end()) return {};
        return it->second;
    }

    std::vector<int64_t> find_range(const ColumnValue& lo,
                                    const ColumnValue& hi) const override {
        if (!is_int(lo) || !is_int(hi)) return {};
        std::vector<int64_t> result;
        int64_t lo_v = get_int(lo), hi_v = get_int(hi);
        auto it = _tree.lower_bound(lo_v);
        while (it != _tree.cend() && it->first < hi_v) {
            for (auto id : it->second) result.push_back(id);
            ++it;
        }
        return result;
    }

    void clear() override { _tree.clear(); }
};

class StrIndex final : public IndexBase {
    B_tree<std::string, std::vector<int64_t>> _tree;

public:
    void insert(const ColumnValue& key, int64_t row_id) override {
        if (!is_string(key)) return;
        const std::string& k = get_string(key);
        auto it = _tree.find(k);
        if (it == _tree.end()) {
            _tree.insert({k, {row_id}});
        } else {
            _tree.at(k).push_back(row_id);
        }
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
        if (it == _tree.end()) return {};
        return it->second;
    }

    std::vector<int64_t> find_range(const ColumnValue& lo,
                                    const ColumnValue& hi) const override {
        if (!is_string(lo) || !is_string(hi)) return {};
        std::vector<int64_t> result;
        auto it = _tree.lower_bound(get_string(lo));
        while (it != _tree.cend() && it->first < get_string(hi)) {
            for (auto id : it->second) result.push_back(id);
            ++it;
        }
        return result;
    }

    void clear() override { _tree.clear(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// JSON serialisation helpers (implements task 2 string interning)
// ─────────────────────────────────────────────────────────────────────────────

static nlohmann::json col_to_json(const ColumnValue& v, StringPool& pool) {
    if (is_null(v))   return nullptr;
    if (is_int(v))    return get_int(v);
    // string: intern and store pool ID wrapped in {"__sid": N}
    uint32_t id = pool.intern(get_string(v));
    return nlohmann::json{{"__sid", id}};
}

static ColumnValue col_from_json(const nlohmann::json& j,
                                 ColumnType type,
                                 const StringPool& pool) {
    if (j.is_null()) return NullValue{};
    if (j.is_number_integer()) return static_cast<int64_t>(j.get<int64_t>());
    if (j.is_object() && j.contains("__sid")) {
        uint32_t id = j["__sid"].get<uint32_t>();
        return pool.resolve(id);
    }
    // fallback: plain string (legacy / round-trip safety)
    if (j.is_string()) return j.get<std::string>();
    // typed fallback
    if (type == ColumnType::INT && j.is_number())
        return static_cast<int64_t>(j.get<double>());
    return NullValue{};
}

static nlohmann::json schema_to_json(const TableSchema& s) {
    nlohmann::json j;
    j["name"]    = s.name;
    j["next_id"] = s.next_id;
    nlohmann::json cols = nlohmann::json::array();
    for (const auto& c : s.columns) {
        nlohmann::json cj;
        cj["name"]     = c.name;
        cj["type"]     = (c.type == ColumnType::INT) ? "int" : "string";
        cj["not_null"] = c.not_null;
        cj["indexed"]  = c.indexed;
        if (c.default_val) {
            const auto& dv = *c.default_val;
            if (is_null(dv))        cj["default"] = nullptr;
            else if (is_int(dv))    cj["default"] = get_int(dv);
            else                    cj["default"] = get_string(dv);
        } else {
            cj["default"] = nullptr;
        }
        cols.push_back(std::move(cj));
    }
    j["columns"] = std::move(cols);
    return j;
}

static TableSchema schema_from_json(const nlohmann::json& j) {
    TableSchema s;
    s.name    = j["name"];
    s.next_id = j["next_id"];
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
        s.columns.push_back(std::move(cd));
    }
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// Table
// ─────────────────────────────────────────────────────────────────────────────

Table::Table(TableSchema schema, std::filesystem::path path, StringPool& pool)
    : _schema(std::move(schema)), _path(std::move(path)), _pool(pool)
{
    // Create index objects for INDEXED columns
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

    if (std::filesystem::exists(_path)) load();
}

// ── private helpers ──────────────────────────────────────────────────────────

void Table::rebuild_indexes() {
    for (auto& e : _indexes) e.index->clear();
    for (auto it = _data.cbegin(); it != _data.cend(); ++it)
        add_to_indexes(it->first, it->second);
}

void Table::add_to_indexes(int64_t row_id, const Row& row) {
    for (auto& e : _indexes) {
        if (e.col_idx < row.size())
            e.index->insert(row[e.col_idx], row_id);
    }
}

void Table::remove_from_indexes(int64_t row_id, const Row& row) {
    for (auto& e : _indexes) {
        if (e.col_idx < row.size())
            e.index->erase(row[e.col_idx], row_id);
    }
}

// ── public API ───────────────────────────────────────────────────────────────

int64_t Table::insert_row(Row row) {
    int64_t id = _schema.next_id++;
    add_to_indexes(id, row);
    _data.insert({id, std::move(row)});
    _dirty = true;
    return id;
}

bool Table::update_row(int64_t row_id, const Row& new_row) {
    auto opt = find_row(row_id);
    if (!opt) return false;
    remove_from_indexes(row_id, *opt);
    _data.insert_or_assign({row_id, new_row});
    add_to_indexes(row_id, new_row);
    _dirty = true;
    return true;
}

bool Table::erase_row(int64_t row_id) {
    auto opt = find_row(row_id);
    if (!opt) return false;
    remove_from_indexes(row_id, *opt);
    _data.erase(row_id);
    _dirty = true;
    return true;
}

std::optional<Row> Table::find_row(int64_t row_id) const {
    auto it = _data.find(row_id);
    if (it == _data.cend()) return std::nullopt;
    return it->second;
}

std::vector<std::pair<int64_t, Row>> Table::scan_all() const {
    std::vector<std::pair<int64_t, Row>> result;
    for (auto it = _data.cbegin(); it != _data.cend(); ++it)
        result.push_back({it->first, it->second});
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

// ── persistence ──────────────────────────────────────────────────────────────

void Table::save() {
    nlohmann::json j;
    j["schema"] = schema_to_json(_schema);

    nlohmann::json rows = nlohmann::json::array();
    for (auto it = _data.cbegin(); it != _data.cend(); ++it) {
        nlohmann::json entry;
        entry["_id"] = it->first;
        nlohmann::json vals = nlohmann::json::array();
        for (size_t ci = 0; ci < it->second.size(); ++ci)
            vals.push_back(col_to_json(it->second[ci], _pool));
        entry["values"] = std::move(vals);
        rows.push_back(std::move(entry));
    }
    j["rows"] = std::move(rows);

    std::filesystem::create_directories(_path.parent_path());
    std::ofstream f(_path);
    if (!f) throw DbError("Table::save: cannot open: " + _path.string());
    f << j.dump(2);
    _dirty = false;

    if (_pool.dirty()) _pool.save();
}

void Table::load() {
    std::ifstream f(_path);
    if (!f) return;
    nlohmann::json j;
    f >> j;

    _schema = schema_from_json(j["schema"]);

    // Recreate index objects to match loaded schema
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

    _data.clear();
    for (const auto& entry : j["rows"]) {
        int64_t id = entry["_id"];
        Row row;
        const auto& vals = entry["values"];
        for (size_t ci = 0; ci < _schema.columns.size() && ci < vals.size(); ++ci)
            row.push_back(col_from_json(vals[ci], _schema.columns[ci].type, _pool));
        _data.insert({id, std::move(row)});
    }
    rebuild_indexes();
    _dirty = false;
}

// ── display helper ────────────────────────────────────────────────────────────

std::string column_value_to_display(const ColumnValue& v) {
    if (is_null(v))   return "NULL";
    if (is_int(v))    return std::to_string(get_int(v));
    return get_string(v);
}

} // namespace db
