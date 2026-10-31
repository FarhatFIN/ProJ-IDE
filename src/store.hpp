// language: C++20, module: store — project list persistence (plain text, transparent)
#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

struct Project {
    std::string name;              // shown as "N. name"
    std::vector<fs::path> files;   // references, files stay where they are
    fs::path last_open;            // file the developer wrote in last
};

class Store {
public:
    std::vector<Project> projects;

    // id() = index + 1 everywhere in the UI.
    bool load();
    bool save() const;

    fs::path file() const;         // full path of the storage file

    void add_project(const std::string& name, const std::vector<fs::path>& files);
    void set_last(int project_idx, const fs::path& f);
    void add_files(int project_idx, const std::vector<fs::path>& files);
    void remove_project(int project_idx);
    void remove_file(int project_idx, const fs::path& f);
};
