// language: C++20, module: install — compiler download / install with progress
#pragma once

#include <string>
#include <atomic>

namespace install {

enum class Outcome { Ok, NetworkError, Failed };

// Checks whether `tool` (g++, gcc, python3, nasm) is available in PATH.
bool tool_present(const std::string& tool, std::string& version_line);

// Blocking install of `tool` from trusted sources (see lang.cpp table).
// Designed to run on a worker thread:
//   * percent: -1 = indeterminate, else 0..100
//   * phase:   human-readable status line ("Загружаем...", "Установка...", ...)
//   * detail:  extra log text on failure
Outcome install(const std::string& tool,
                std::atomic<int>& percent,
                std::atomic<bool>& cancel,
                std::string& phase,
                std::string& detail);

} // namespace install
