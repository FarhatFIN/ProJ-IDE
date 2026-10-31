// language: C++20, module: util — process exec, paths, PATH lookup, network probe
// ProJ IDE — compact cross-platform IDE
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace fs = std::filesystem;

namespace util {

enum class OS { Linux, Windows, MacOS };
OS os();

// ---- strings ----
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char sep);
bool starts_with(const std::string& s, const std::string& p);
bool ends_with_ci(const std::string& s, const std::string& p);
std::string to_lower(std::string s);
std::string replace_all(std::string s, const std::string& from, const std::string& to);

// ---- paths ----
fs::path home_dir();
fs::path config_dir();                     // per-OS app config directory
std::string path_str(const fs::path& p);   // native display string (utf-8 on posix)

// ---- process ----
struct ProcResult {
    int exit_code = -1;
    std::string out;          // merged stdout+stderr
    bool timed_out = false;
};

// Runs argv[0] with arguments, captures merged output, enforces timeout (seconds).
// `stdin_data` (optional) is fed to the child's stdin, then closed.
ProcResult exec_capture(const std::vector<std::string>& argv,
                        const std::optional<fs::path>& cwd = std::nullopt,
                        int timeout_s = 60,
                        const std::string& stdin_data = {});

// Runs a shell one-liner in a new terminal window, detached (survives parent).
void run_detached_terminal(const std::string& shell_cmd, const fs::path& workdir);

// Opens a URL in the system browser.
void open_url(const std::string& url);

// True when the process searched PATH and found an executable.
bool find_in_path(const std::string& name, fs::path& out);

// ---- network ----
// Blocking DNS probe; call from a worker thread. timeout ignored for DNS (short by design).
bool net_probe(const std::string& host, const std::string& port);

// Last non-empty line's trailing percent, e.g. "Progress: [ 42% ]" -> 42. -1 if none.
int extract_percent(const std::string& chunk);

} // namespace util
