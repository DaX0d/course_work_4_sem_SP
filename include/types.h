#pragma once
#include <variant>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <stdexcept>

namespace db {

enum class ColumnType { INT, STRING };

using NullValue = std::monostate;
using IntValue = int64_t;
using StringValue = std::string;

// A cell in a row: NULL | integer | string
using ColumnValue = std::variant<NullValue, IntValue, StringValue>;

// A full row is an ordered list of column values
using Row = std::vector<ColumnValue>;

inline bool is_null (const ColumnValue& v) noexcept { return std::holds_alternative<NullValue>(v); }
inline bool is_int (const ColumnValue& v) noexcept { return std::holds_alternative<IntValue>(v); }
inline bool is_string(const ColumnValue& v) noexcept { return std::holds_alternative<StringValue>(v); }

inline IntValue get_int(const ColumnValue& v) { return std::get<IntValue>(v); }
inline const StringValue& get_string(const ColumnValue& v) { return std::get<StringValue>(v); }

std::string column_value_to_display(const ColumnValue& v);

struct DbError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace db
