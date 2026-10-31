// language: C++20, module: build — command construction, execution, error parsing
#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

struct BuildError {
    std::string file;   // empty = general
    int line = 0;       // 1-based, 0 = unknown
    int col = 0;
    bool is_error = true;   // false = warning/note
    std::string msg;
};

struct BuildResult {
    bool ok = false;
    int err_count = 0;
    int warn_count = 0;
    std::string log;
    std::vector<BuildError> errors;
    fs::path produced;      // binary path on success (not set for python)
};

BuildResult build_file(const fs::path& src, const std::string& stdin_data = {});

// Parses gcc/nasm-style "file:line[:col]: severity: msg" and python tracebacks.
std::vector<BuildError> parse_compiler_log(const std::string& log);
