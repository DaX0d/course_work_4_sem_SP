#pragma once
#include "table.h"
#include "string_pool.h"
#include "access_log.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <filesystem>

namespace db {

class Database {
    std::string _name;
    std::filesystem::path _path;
    std::unordered_map<std::string, std::unique_ptr<Table>> _tables;
    StringPool _pool;
    AccessLog _log;

    void discover_tables();

public:
    Database(std::string name, std::filesystem::path path);
    ~Database();

    const std::string& name() const noexcept { return _name; }

    void create_table(TableSchema schema);
    void drop_table(const std::string& tname);
    Table* get_table(const std::string& tname);
    bool has_table(const std::string& tname) const;

    AccessLog& access_log() { return _log; }

    void flush_all();
};

} // namespace db
