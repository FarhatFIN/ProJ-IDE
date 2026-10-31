// language: C++20, module: jsonlite — implementation
#include "jsonlite.hpp"

#include <cctype>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace json {

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof(b), "\\u%04x", c);
                out += b;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

std::string str(const std::string& s) { return "\"" + escape(s) + "\""; }

std::string num(double d) {
    if (std::isnan(d) || std::isinf(d)) return "null";
    if (d == static_cast<long long>(d)) return std::to_string(static_cast<long long>(d));
    char b[32];
    std::snprintf(b, sizeof(b), "%.6g", d);
    return b;
}

std::string boolean(bool b) { return b ? "true" : "false"; }

std::string arr(const std::vector<std::string>& parts) {
    std::string out = "[";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += ",";
        out += parts[i];
    }
    out += "]";
    return out;
}

std::string obj(std::vector<std::pair<std::string, std::string>> kv) {
    std::string out = "{";
    for (size_t i = 0; i < kv.size(); ++i) {
        if (i) out += ",";
        out += str(kv[i].first) + ":" + kv[i].second;
    }
    out += "}";
    return out;
}

namespace {

struct Parser {
    const char* p;
    const char* end;

    void skip_ws() {
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
    }

    bool parse_value(Value& out) {
        skip_ws();
        if (p >= end) return false;
        if (*p == '{') { out.v.emplace<Object>(); return parse_object(std::get<Object>(out.v)); }
        if (*p == '[') { out.v.emplace<Array>(); return parse_array(std::get<Array>(out.v)); }
        if (*p == '"') { out.v = std::string(); return parse_string(std::get<std::string>(out.v)); }
        if (*p == 't') { if (end - p >= 4 && !strncmp(p, "true", 4)) { out.v = true; p += 4; return true; } return false; }
        if (*p == 'f') { if (end - p >= 5 && !strncmp(p, "false", 5)) { out.v = false; p += 5; return true; } return false; }
        if (*p == 'n') { if (end - p >= 4 && !strncmp(p, "null", 4)) { out.v = nullptr; p += 4; return true; } return false; }
        // number
        const char* start = p;
        if (*p == '-') ++p;
        while (p < end && (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) ++p;
        if (p == start) return false;
        out.v = std::strtod(std::string(start, p).c_str(), nullptr);
        return true;
    }

    bool parse_string(std::string& out) {
        if (p >= end || *p != '"') return false;
        ++p;
        out.clear();
        while (p < end) {
            char c = *p++;
            if (c == '"') return true;
            if (c == '\\') {
                if (p >= end) return false;
                char e = *p++;
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (end - p < 4) return false;
                    unsigned cp = static_cast<unsigned>(std::strtoul(std::string(p, p + 4).c_str(), nullptr, 16));
                    p += 4;
                    // surrogate pair
                    if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        unsigned lo = static_cast<unsigned>(std::strtoul(std::string(p + 2, p + 6).c_str(), nullptr, 16));
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            p += 6;
                        }
                    }
                    // encode utf-8
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xF0 | (cp >> 18));
                        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: return false;
                }
            } else {
                out += c;
            }
        }
        return false;
    }

    bool parse_array(Array& out) {
        ++p; // [
        skip_ws();
        if (p < end && *p == ']') { ++p; return true; }
        for (;;) {
            Value v;
            if (!parse_value(v)) return false;
            out.push_back(std::move(v));
            skip_ws();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == ']') { ++p; return true; }
            return false;
        }
    }

    bool parse_object(Object& out) {
        ++p; // {
        skip_ws();
        if (p < end && *p == '}') { ++p; return true; }
        for (;;) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (p >= end || *p != ':') return false;
            ++p;
            Value v;
            if (!parse_value(v)) return false;
            out[key] = std::move(v);
            skip_ws();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == '}') { ++p; return true; }
            return false;
        }
    }
};

} // namespace

bool parse(const std::string& text, Value& out) {
    Parser ps{text.data(), text.data() + text.size()};
    if (!ps.parse_value(out)) return false;
    ps.skip_ws();
    return ps.p == ps.end;
}

} // namespace json
