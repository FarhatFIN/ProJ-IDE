// language: C++20, module: store — implementation
//
// Format (plain text, human readable — "transparent" per spec):
//   # comment
//   project <name...>
//   file <path...>
//   last <path...>
// `file`/`last` lines belong to the most recent `project` line.
#include "store.hpp"
#include "util.hpp"

#include <fstream>
#include <sstream>

fs::path Store::file() const { return util::config_dir() / "projects.txt"; }

bool Store::load() {
    projects.clear();
    std::error_code ec;
    fs::create_directories(util::config_dir(), ec);
    std::ifstream f(file());
    if (!f) return false; // first run — empty store is normal
    Project* cur = nullptr;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = util::trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (util::starts_with(line, "project ")) {
            projects.push_back(Project{line.substr(8), {}, {}});
            cur = &projects.back();
        } else if (util::starts_with(line, "file ")) {
            if (cur) cur->files.push_back(fs::path(util::trim(line.substr(5))));
        } else if (util::starts_with(line, "last ")) {
            if (cur) cur->last_open = fs::path(util::trim(line.substr(5)));
        }
    }
    return true;
}

bool Store::save() const {
    std::error_code ec;
    fs::create_directories(util::config_dir(), ec);
    std::ofstream f(file(), std::ios::trunc);
    if (!f) return false;
    f << "# ProJ IDE — projects. Plain text on purpose: no hidden state.\n";
    for (const auto& p : projects) {
        f << "project " << p.name << "\n";
        for (const auto& fp : p.files) f << "file " << util::path_str(fp) << "\n";
        if (!p.last_open.empty()) f << "last " << util::path_str(p.last_open) << "\n";
    }
    return bool(f);
}

void Store::add_project(const std::string& name, const std::vector<fs::path>& files) {
    Project p;
    p.name = name;
    p.files = files;
    if (!files.empty()) p.last_open = files.front();
    projects.push_back(std::move(p));
}

void Store::set_last(int idx, const fs::path& f) {
    if (idx >= 0 && idx < static_cast<int>(projects.size())) projects[idx].last_open = f;
}

void Store::add_files(int idx, const std::vector<fs::path>& files) {
    if (idx < 0 || idx >= static_cast<int>(projects.size())) return;
    for (const auto& f : files) {
        bool dup = false;
        for (const auto& existing : projects[idx].files)
            if (existing == f) { dup = true; break; }
        if (!dup) projects[idx].files.push_back(f);
    }
    if (!files.empty() && projects[idx].last_open.empty()) projects[idx].last_open = files.front();
}

void Store::remove_project(int idx) {
    if (idx >= 0 && idx < static_cast<int>(projects.size()))
        projects.erase(projects.begin() + idx);
}

void Store::remove_file(int idx, const fs::path& f) {
    if (idx < 0 || idx >= static_cast<int>(projects.size())) return;
    auto& v = projects[idx].files;
    for (auto it = v.begin(); it != v.end(); ++it)
        if (*it == f) { v.erase(it); break; }
    if (projects[idx].last_open == f)
        projects[idx].last_open = v.empty() ? fs::path{} : v.front();
}
