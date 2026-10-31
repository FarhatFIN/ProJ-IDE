// language: C++20, module: embed — ustar-in-binary reader
#include "embed.hpp"

#include <cstring>
#include <map>
#include <mutex>

extern "C" const unsigned char _binary_web_tar_start[];
extern "C" const unsigned char _binary_web_tar_end[];

namespace embed {

namespace {

struct Entry { size_t offset = 0, size = 0; };
std::map<std::string, Entry> g_index;
std::once_flag g_once;

unsigned long long parse_octal(const char* field, size_t n) {
    unsigned long long v = 0;
    for (size_t i = 0; i < n; ++i) {
        char c = field[i];
        if (c == 0 || c == ' ') {
            if (v) break;   // trailing padding
            continue;
        }
        if (c < '0' || c > '7') break;
        v = v * 8 + static_cast<unsigned long long>(c - '0');
    }
    return v;
}

std::string clean_name(const char* name_field, const char* prefix_field) {
    std::string name(name_field, strnlen(name_field, 100));
    if (prefix_field && prefix_field[0]) {
        std::string prefix(prefix_field, strnlen(prefix_field, 155));
        if (!prefix.empty()) name = prefix + "/" + name;
    }
    // entries were added as "tar -cf web.tar -C web ." → names start with "./"
    if (name.rfind("./", 0) == 0) name.erase(0, 2);
    while (!name.empty() && name.back() == '/') name.pop_back();
    return name;
}

void build_index() {
    const char* p = reinterpret_cast<const char*>(_binary_web_tar_start);
    const char* end = reinterpret_cast<const char*>(_binary_web_tar_end);

    std::string longname; // pending GNU 'L' entry
    while (p + 512 <= end) {
        if (p[0] == 0) { p += 512; continue; } // zero block
        const char* name_f = p + 0;
        char size_f[13]; std::memcpy(size_f, p + 124, 12); size_f[12] = 0;
        char typeflag = p[156];
        const char* prefix_f = p + 345;
        unsigned long long size = parse_octal(size_f, 12);
        const char* data = p + 512;
        unsigned long long padded = (size + 511) / 512 * 512;

        if (typeflag == 'L') { // GNU long name: data is the real name of the next entry
            longname.assign(data, size);
            if (!longname.empty() && longname.back() == '\0') longname.pop_back();
            p = data + padded;
            continue;
        }
        if (typeflag == '0' || typeflag == 0) {
            std::string name = clean_name(name_f, prefix_f);
            if (!longname.empty()) { name = longname; longname.clear(); }
            if (!name.empty() && size > 0) g_index[name] = {static_cast<size_t>(data - reinterpret_cast<const char*>(_binary_web_tar_start)), static_cast<size_t>(size)};
        }
        p = data + padded;
    }
}

} // namespace

std::optional<std::string> get(const std::string& path) {
    std::call_once(g_once, build_index);
    auto it = g_index.find(path);
    if (it == g_index.end()) return std::nullopt;
    const char* base = reinterpret_cast<const char*>(_binary_web_tar_start);
    return std::string(base + it->second.offset, it->second.size);
}

} // namespace embed
