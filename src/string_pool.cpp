#include "string_pool.h"
#include "types.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace db {

StringPool::StringPool(std::filesystem::path path) : _path(std::move(path)) {
    if (std::filesystem::exists(_path)) load();
}

uint32_t StringPool::intern(const std::string& s) {
    auto it = _str_to_id.find(s);
    if (it != _str_to_id.end()) return it->second;

    uint32_t id = static_cast<uint32_t>(_id_to_str.size());
    _id_to_str.push_back(s);
    _str_to_id[s] = id;
    _dirty = true;
    return id;
}

const std::string& StringPool::resolve(uint32_t id) const {
    if (id >= _id_to_str.size())
        throw DbError("StringPool: invalid id " + std::to_string(id));
    return _id_to_str[id];
}

void StringPool::save() {
    nlohmann::json j;
    j["pool"] = _id_to_str;
    std::ofstream f(_path);
    if (!f) throw DbError("StringPool: cannot open for write: " + _path.string());
    f << j.dump(2);
    _dirty = false;
}

void StringPool::load() {
    std::ifstream f(_path);
    if (!f) return;
    nlohmann::json j;
    f >> j;
    _id_to_str = j["pool"].get<std::vector<std::string>>();
    _str_to_id.clear();
    for (uint32_t i = 0; i < _id_to_str.size(); ++i)
        _str_to_id[_id_to_str[i]] = i;
    _dirty = false;
}

} // namespace db
