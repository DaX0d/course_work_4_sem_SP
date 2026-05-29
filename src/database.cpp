#include "database.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace db {

Database::Database(std::string name, std::filesystem::path path)
    : _name(std::move(name))
    , _path(std::move(path))
    , _pool(_path / "strings.json")
    , _log (_path / "access.log")
{
    std::filesystem::create_directories(_path);
    discover_tables();
}

Database::~Database() {
    flush_all();
}

void Database::discover_tables() {
    if (!std::filesystem::exists(_path)) return;
    for (const auto& entry : std::filesystem::directory_iterator(_path)) {
        // Each table has a .dat file; use that as the discovery marker
        if (entry.path().extension() != ".dat") continue;
        const std::string tname = entry.path().stem().string();

        auto schema_path = _path / (tname + ".schema.json");
        if (!std::filesystem::exists(schema_path)) continue;

        // Read schema to construct the TableSchema object
        std::ifstream f(schema_path);
        if (!f) continue;
        nlohmann::json j;
        try { f >> j; } catch (...) { continue; }
        if (!j.contains("columns")) continue;

        TableSchema schema;
        schema.name    = j["name"];
        schema.next_id = j["next_id"];
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
            schema.columns.push_back(std::move(cd));
        }

        auto base = _path / tname;
        _tables[tname] = std::make_unique<Table>(std::move(schema), base, _pool);
    }
}

void Database::create_table(TableSchema schema) {
    if (has_table(schema.name))
        throw DbError("Table already exists: " + schema.name);
    auto base = _path / schema.name;
    auto tbl  = std::make_unique<Table>(schema, base, _pool);
    tbl->save(); // writes .dat (empty), .idx, .schema.json
    _tables[schema.name] = std::move(tbl);
}

void Database::drop_table(const std::string& tname) {
    auto it = _tables.find(tname);
    if (it == _tables.end())
        throw DbError("Table not found: " + tname);
    it->second->drop_files();
    _tables.erase(it);
}

Table* Database::get_table(const std::string& tname) {
    auto it = _tables.find(tname);
    if (it == _tables.end())
        throw DbError("Table not found: " + tname);
    return it->second.get();
}

bool Database::has_table(const std::string& tname) const {
    return _tables.count(tname) > 0;
}

void Database::flush_all() {
    for (auto& [name, tbl] : _tables)
        if (tbl->dirty()) tbl->save();
    if (_pool.dirty()) _pool.save();
}

} // namespace db
