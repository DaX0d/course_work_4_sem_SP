#pragma once
#include "database.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <filesystem>

namespace db {

class StorageManager {
    std::filesystem::path _data_root;
    std::unordered_map<std::string, std::unique_ptr<Database>> _databases;
    std::string _current_db;

    void discover_databases();

public:
    explicit StorageManager(std::filesystem::path data_root = "./data");
    ~StorageManager();

    void create_database(const std::string& name);
    void drop_database(const std::string& name);
    void use_database(const std::string& name);

    Database* current_db();
    const std::string& current_db_name() const noexcept { return _current_db; }
    bool has_current_db() const noexcept { return !_current_db.empty(); }

    Database* get_database(const std::string& name);
    bool has_database(const std::string& name) const;
};

} // namespace db
