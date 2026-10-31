// language: C++20, module: jsonlite — minimal JSON writer/parser (no deps)
#pragma once

#include <map>
#include <utility>
#include <string>
#include <variant>
#include <vector>

namespace json {

std::string escape(const std::string& s);

struct Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

struct Value {
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v;

    bool is_string() const { return std::holds_alternative<std::string>(v); }
    bool is_object() const { return std::holds_alternative<Object>(v); }
    bool is_array() const { return std::holds_alternative<Array>(v); }
    bool is_bool() const { return std::holds_alternative<bool>(v); }
    bool is_number() const { return std::holds_alternative<double>(v); }

    const std::string& as_string() const { return std::get<std::string>(v); }
    double as_number() const { return std::get<double>(v); }
    bool as_bool() const { return std::get<bool>(v); }
    const Object& as_object() const { return std::get<Object>(v); }
    const Array& as_array() const { return std::get<Array>(v); }

    const Value* find(const std::string& key) const {
        if (!is_object()) return nullptr;
        const auto& o = as_object();
        auto it = o.find(key);
        return it == o.end() ? nullptr : &it->second;
    }
};

// parser: returns false on malformed input
bool parse(const std::string& text, Value& out);

// writer helpers
std::string str(const std::string& s);      // quoted+escaped
std::string num(double d);
std::string boolean(bool b);
std::string arr(const std::vector<std::string>& parts);  // [a,b,c]
std::string obj(std::vector<std::pair<std::string, std::string>> kv); // {"k":v,...}

} // namespace json
