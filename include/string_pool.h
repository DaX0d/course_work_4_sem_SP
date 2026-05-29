#pragma once
// Task 2: String interning / deduplication.
// Identical string values share a single copy in the pool.
// The pool is persisted to a JSON file alongside the database.
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <cstdint>

namespace db {

class StringPool {
    std::unordered_map<std::string, uint32_t> _str_to_id;
    std::vector<std::string> _id_to_str;
    std::filesystem::path _path;
    bool _dirty = false;

public:
    explicit StringPool(std::filesystem::path path);

    // Return (or create) the pool ID for a string value
    uint32_t intern(const std::string& s);

    // Resolve pool ID → string
    const std::string& resolve(uint32_t id) const;

    size_t size() const noexcept { return _id_to_str.size(); }
    bool dirty() const noexcept { return _dirty; }

    void save();
    void load();
};

} // namespace db
