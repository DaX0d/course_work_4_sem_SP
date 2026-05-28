#include "access_log.h"
#include "types.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace db {

AccessLog::AccessLog(std::filesystem::path path) : _path(std::move(path)) {}

static std::string now_string() {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()) % 1000;
    std::ostringstream ss;
    std::tm tm_val{};
#if defined(_WIN32)
    localtime_s(&tm_val, &tt);
#else
    localtime_r(&tt, &tm_val);
#endif
    ss << std::put_time(&tm_val, "%Y.%m.%d-%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void AccessLog::log(const std::string& db_name,
                    const std::string& table_name,
                    const std::string& operation,
                    bool               success) {
    std::ofstream f(_path, std::ios::app);
    if (!f) return; // best-effort; don't throw from a log function
    f << now_string()
      << " | " << (db_name.empty()    ? "-" : db_name)
      << " | " << (table_name.empty() ? "-" : table_name)
      << " | " << operation
      << " | " << (success ? "SUCCESS" : "FAIL")
      << '\n';
}

} // namespace db
