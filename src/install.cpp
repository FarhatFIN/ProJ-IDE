// language: C++20, module: install — trusted-source installer per OS
//
// Strategy table (kept next to lang.cpp trusted::*):
//   Linux debian-family : pkexec apt-get install <pkg>   (polkit password dialog)
//   Linux fedora-family : pkexec dnf install <pkg>
//   Windows             : winget install <id>, fallback: pinned official URL
//   macOS               : xcode-select --install (clang), pinned .pkg via curl, brew
//
// Progress: dnf / apt (APT::Progress-Fancy) / curl emit percents — parsed.
// When the backend gives no numbers the UI shows an indeterminate bar (percent = -1).
#include "install.hpp"
#include "lang.hpp"
#include "util.hpp"

#include <fstream>
#include <thread>
#include <chrono>
#include <functional>

namespace install {

bool tool_present(const std::string& tool, std::string& version_line) {
    fs::path p;
    if (!util::find_in_path(tool, p)) return false;
    auto r = util::exec_capture({p.string(), "--version"}, std::nullopt, 5);
    if (r.exit_code != 0) {
        // nasm uses -v, python3 --version works; try -v as fallback
        auto r2 = util::exec_capture({p.string(), "-v"}, std::nullopt, 5);
        version_line = util::trim(r2.out.substr(0, r2.out.find('\n')));
        return true;
    }
    version_line = util::trim(r.out.substr(0, r.out.find('\n')));
    return true;
}

namespace {

void pump_percent(std::atomic<int>& percent, const std::string& text) {
    int v = util::extract_percent(text);
    if (v >= 0) percent.store(v);
}

// runs argv, feeds output into the percent pump, returns exit status
bool run_install_cmd(const std::vector<std::string>& argv,
                     std::atomic<int>& percent,
                     std::string& log) {
    auto r = util::exec_capture(argv, std::nullopt, 1800);
    pump_percent(percent, r.out);
    log += r.out;
    return r.exit_code == 0 && !r.timed_out;
}

Outcome install_linux_debian(const std::string& tool, std::atomic<int>& percent,
                             std::string& phase, std::string& detail) {
    std::string pkg = trusted::pkg_debian(tool);
    if (pkg.empty()) {
        detail = "нет доверенного источника для " + tool;
        return Outcome::Failed;
    }
    fs::path pkexec, sudo, apt;
    const bool have_pkexec = util::find_in_path("pkexec", pkexec);
    const bool have_sudo = util::find_in_path("sudo", sudo);
    if (!util::find_in_path("apt-get", apt)) {
        detail = "apt-get не найден (это дистрибутив Debian-семейства?)";
        return Outcome::Failed;
    }

    phase = "Обновление списка пакетов...";
    bool ok = false;
    if (have_pkexec)
        ok = run_install_cmd({pkexec.string(), "apt-get", "update", "-qq"}, percent, detail);
    else if (have_sudo)
        ok = run_install_cmd({sudo.string(), "apt-get", "update", "-qq"}, percent, detail);
    else {
        detail = "ни pkexec, ни sudo не найдены — установка невозможна";
        return Outcome::Failed;
    }
    if (!ok && detail.find("E:") != std::string::npos) return Outcome::Failed;

    phase = "Загружаем...";
    percent.store(-1);
    bool res = have_pkexec
        ? run_install_cmd({pkexec.string(), "apt-get", "install", "-y",
                           "-o", "APT::Progress-Fancy=1", pkg}, percent, detail)
        : run_install_cmd({sudo.string(), "apt-get", "install", "-y",
                           "-o", "APT::Progress-Fancy=1", pkg}, percent, detail);
    return res ? Outcome::Ok : Outcome::Failed;
}

Outcome install_linux_fedora(const std::string& tool, std::atomic<int>& percent,
                             std::string& phase, std::string& detail) {
    std::string pkg = trusted::pkg_fedora(tool);
    if (pkg.empty()) {
        detail = "нет доверенного источника для " + tool;
        return Outcome::Failed;
    }
    fs::path pkexec, dnf;
    const bool have_pkexec = util::find_in_path("pkexec", pkexec);
    if (!util::find_in_path("dnf", dnf)) {
        detail = "dnf не найден";
        return Outcome::Failed;
    }
    phase = "Загружаем...";
    std::vector<std::string> base = have_pkexec
        ? std::vector<std::string>{pkexec.string(), "dnf", "install", "-y", pkg}
        : std::vector<std::string>{dnf.string(), "install", "-y", pkg};
    return run_install_cmd(base, percent, detail) ? Outcome::Ok : Outcome::Failed;
}

Outcome install_windows(const std::string& tool, std::atomic<int>& percent,
                        std::string& phase, std::string& detail) {
    fs::path winget;
    std::string id = trusted::winget_id(tool);
    if (!id.empty() && util::find_in_path("winget", winget)) {
        phase = "Загружаем...";
        std::vector<std::string> argv = {
            winget.string(), "install", "-e", "--id", id,
            "--accept-source-agreements", "--accept-package-agreements", "--silent"};
        return run_install_cmd(argv, percent, detail) ? Outcome::Ok : Outcome::Failed;
    }
    // direct download fallback: PowerShell Invoke-WebRequest emits no percents
    // reliably; use curl.exe (ships with Windows 10+) with -# progress
    fs::path curl;
    if (util::find_in_path("curl", curl)) {
        const std::string url = trusted::direct_url_windows(tool);
        if (url.empty()) { detail = "нет доверенной ссылки для " + tool; return Outcome::Failed; }
        const std::string dest = (util::home_dir() / "Downloads" / "proj-ide-setup.bin").string();
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(dest).parent_path(), ec);
        phase = "Загружаем...";
        bool ok = run_install_cmd({curl.string(), "-L", "-#", "-o", dest, url},
                                  percent, detail);
        if (!ok) return Outcome::Failed;
        phase = "Установка...";
        util::run_detached_terminal(
            "start /wait \"\" \"" + dest + "\"", util::home_dir() / "Downloads");
        return Outcome::Ok;
    }
    detail = "ни winget, ни curl не найдены";
    return Outcome::Failed;
}

Outcome install_macos(const std::string& tool, std::atomic<int>& percent,
                      std::string& phase, std::string& detail) {
    if (tool == "g++" || tool == "gcc") {
        phase = "Установка инструментов разработчика (подтвердите в окне Apple)...";
        util::run_detached_terminal("xcode-select --install", util::home_dir());
        // give Apple's dialog time; verification happens back in the UI flow
        return Outcome::Ok;
    }
    fs::path curl, brew;
    if (util::find_in_path("brew", brew)) {
        phase = "Загружаем...";
        const char* pkg = tool == "python3" ? "python" : tool.c_str();
        return run_install_cmd({brew.string(), "install", pkg}, percent, detail)
            ? Outcome::Ok : Outcome::Failed;
    }
    if (util::find_in_path("curl", curl)) {
        const std::string url = trusted::direct_url_macos(tool);
        if (url.empty()) { detail = "нет доверенной ссылки для " + tool; return Outcome::Failed; }
        const std::string dest = "/tmp/proj-ide-setup.pkg";
        phase = "Загружаем...";
        bool ok = run_install_cmd({curl.string(), "-L", "-#", "-o", dest, url},
                                  percent, detail);
        if (!ok) return Outcome::Failed;
        phase = "Установка (подтвердите в окне установщика)...";
        util::exec_capture({"open", dest}, std::nullopt, 10);
        return Outcome::Ok;
    }
    detail = "ни brew, ни curl не найдены";
    return Outcome::Failed;
}

} // namespace

Outcome install(const std::string& tool,
                std::atomic<int>& percent,
                std::atomic<bool>& cancel,
                std::string& phase,
                std::string& detail) {
    (void)cancel; // cancel is wired in the UI thread; long ops are auto-kill on timeout

    // network gate first — spec: "Ошибка сети"
    if (!util::net_probe(trusted::probe_host(), "443")) return Outcome::NetworkError;

    switch (util::os()) {
    case util::OS::Linux: {
        std::error_code ec;
        if (std::filesystem::exists("/etc/fedora-release", ec))
            return install_linux_fedora(tool, percent, phase, detail);
        if (std::filesystem::exists("/etc/debian_version", ec))
            return install_linux_debian(tool, percent, phase, detail);
        detail = "неподдерживаемый дистрибутив: нет /etc/debian_version и /etc/fedora-release";
        return Outcome::Failed;
    }
    case util::OS::Windows:
        return install_windows(tool, percent, phase, detail);
    case util::OS::MacOS:
        return install_macos(tool, percent, phase, detail);
    }
    return Outcome::Failed;
}

} // namespace install
