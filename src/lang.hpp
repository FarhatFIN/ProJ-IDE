// language: C++20, module: lang — file-type detection, tool mapping, trusted sources
#pragma once

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

enum class Lang { Cpp, C, Python, Asm, Markdown, Text, Unknown };

Lang lang_by_ext(const fs::path& p);
const char* lang_name(Lang l);      // human readable, RU
const char* lang_label(Lang l);     // short tag for the tab strip, e.g. "C++"

// The tool that must exist to build/run this language, e.g. "g++".
const char* tool_for(Lang l);

// Compiler is meaningful only for these languages.
bool lang_compilable(Lang l);

// Trusted install source table — see install.cpp for the per-OS strategy.
namespace trusted {
// Debian-family package name for a tool, empty if unknown.
std::string pkg_debian(const std::string& tool);
// Fedora-family package name for a tool.
std::string pkg_fedora(const std::string& tool);
// Windows: winget package id (empty if none).
std::string winget_id(const std::string& tool);
// Windows: direct download URL (trusted, pinned).
std::string direct_url_windows(const std::string& tool);
// macOS: direct download URL (trusted, pinned).
std::string direct_url_macos(const std::string& tool);
// DNS probe host used per OS before attempting downloads.
const char* probe_host();
} // namespace trusted
