#include "storage_manager.h"

namespace db {

StorageManager::StorageManager(std::filesystem::path data_root)
    : _data_root(std::move(data_root))
{
    std::filesystem::create_directories(_data_root);
    discover_databases();
}

StorageManager::~StorageManager() {
    for (auto& [n, db] : _databases)
        db->flush_all();
}

void StorageManager::discover_databases() {
    if (!std::filesystem::exists(_data_root)) return;
    for (const auto& e : std::filesystem::directory_iterator(_data_root)) {
        if (!e.is_directory()) continue;
        std::string n = e.path().filename().string();
        _databases[n] = std::make_unique<Database>(n, e.path());
    }
}

void StorageManager::create_database(const std::string& name) {
    if (has_database(name))
        throw DbError("Database already exists: " + name);
    auto path = _data_root / name;
    _databases[name] = std::make_unique<Database>(name, path);
}

void StorageManager::drop_database(const std::string& name) {
    if (!has_database(name))
        throw DbError("Database not found: " + name);
    if (_current_db == name) _current_db.clear();
    _databases.erase(name);
    std::filesystem::remove_all(_data_root / name);
}

void StorageManager::use_database(const std::string& name) {
    if (!has_database(name))
        throw DbError("Database not found: " + name);
    _current_db = name;
}

Database* StorageManager::current_db() {
    if (_current_db.empty())
        throw DbError("No database selected (use USE <db>)");
    return _databases.at(_current_db).get();
}

Database* StorageManager::get_database(const std::string& name) {
    auto it = _databases.find(name);
    if (it == _databases.end())
        throw DbError("Database not found: " + name);
    return it->second.get();
}

bool StorageManager::has_database(const std::string& name) const {
    return _databases.count(name) > 0;
}

} // namespace db
