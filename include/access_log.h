#pragma once
// Task 7: Access log.
// Logs every operation with: timestamp, DB name, table name, operation, result.
#include <string>
#include <filesystem>

namespace db {

class AccessLog {
    std::filesystem::path _path;

public:
    explicit AccessLog(std::filesystem::path path);

    void log(const std::string& db_name,
             const std::string& table_name,
             const std::string& operation,
             bool               success);
};

} // namespace db
