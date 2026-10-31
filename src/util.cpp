// language: C++20, module: util — POSIX/Windows implementations
#include "util.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <ctime>

#include <algorithm>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <ws2tcpip.h>
#  include <shellapi.h>
#else
#  include <unistd.h>
#  include <sys/wait.h>
#  include <sys/types.h>
#  include <poll.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <netdb.h>
#  include <sys/socket.h>
#endif

namespace util {

OS os() {
#ifdef _WIN32
    return OS::Windows;
#elif defined(__APPLE__)
    return OS::MacOS;
#else
    return OS::Linux;
#endif
}

// ---------- strings ----------

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

bool starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ends_with_ci(const std::string& s, const std::string& p) {
    if (s.size() < p.size()) return false;
    return to_lower(s.substr(s.size() - p.size())) == to_lower(p);
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// ---------- paths ----------

fs::path home_dir() {
#ifdef _WIN32
    const char* hd = std::getenv("USERPROFILE");
    if (hd) return fs::path(hd);
    const char* hp = std::getenv("HOMEDRIVE");
    const char* hpath = std::getenv("HOMEPATH");
    if (hp && hpath) return fs::path(std::string(hp) + hpath);
    return fs::path("C:/");
#else
    const char* h = std::getenv("HOME");
    return h ? fs::path(h) : fs::path("/");
#endif
}

fs::path config_dir() {
    fs::path base;
    switch (os()) {
#ifdef _WIN32
    case OS::Windows: {
        const char* ad = std::getenv("APPDATA");
        base = ad ? fs::path(ad) : home_dir() / "AppData" / "Roaming";
        return base / "ProJ IDE";
    }
#elif defined(__APPLE__)
    case OS::MacOS:
        return home_dir() / "Library" / "Application Support" / "ProJ IDE";
#endif
    default: {
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        base = xdg && *xdg ? fs::path(xdg) : home_dir() / ".config";
        return base / "proj-ide";
    }
    }
    return base;
}

std::string path_str(const fs::path& p) {
#ifdef _WIN32
    // console-free windows: keep it simple, ANSI code page
    return p.string();
#else
    return p.string();
#endif
}

// ---------- process (POSIX) ----------

#ifndef _WIN32
ProcResult exec_capture(const std::vector<std::string>& argv,
                        const std::optional<fs::path>& cwd,
                        int timeout_s,
                        const std::string& stdin_data) {
    ProcResult res;
    if (argv.empty()) return res;

    int out_fds[2];
    if (pipe(out_fds) != 0) { res.out = "[exec] pipe() failed\n"; return res; }
    int in_fds[2] = {-1, -1};
    const bool has_stdin = !stdin_data.empty();
    if (has_stdin && pipe(in_fds) != 0) {
        close(out_fds[0]); close(out_fds[1]);
        res.out = "[exec] pipe() failed\n";
        return res;
    }

    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        close(out_fds[0]); close(out_fds[1]);
        if (has_stdin) { close(in_fds[0]); close(in_fds[1]); }
        res.out = "[exec] fork() failed\n";
        return res;
    }
    if (pid == 0) {
        setpgid(0, 0);
        if (cwd && !cwd->empty()) {
            if (chdir(cwd->string().c_str()) != 0) _exit(126);
        }
        dup2(out_fds[1], STDOUT_FILENO);
        dup2(out_fds[1], STDERR_FILENO);
        if (has_stdin) dup2(in_fds[0], STDIN_FILENO);
        close(out_fds[0]);
        close(out_fds[1]);
        if (has_stdin) { close(in_fds[0]); close(in_fds[1]); }
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        execvp(args[0], args.data());
        _exit(127);
    }

    close(out_fds[1]);
    if (has_stdin) {
        close(in_fds[0]);
        // child's stdin buffer is 64K on Linux; inputs beyond that would block —
        // IDE stdin stays small, guard anyway
        if (stdin_data.size() <= 32 * 1024) {
            size_t off = 0;
            while (off < stdin_data.size()) {
                ssize_t n = ::write(in_fds[1], stdin_data.data() + off, stdin_data.size() - off);
                if (n < 0) { if (errno == EINTR) continue; break; }
                off += static_cast<size_t>(n);
            }
        }
        close(in_fds[1]);
    }
    setpgid(pid, pid); // own group so a timeout kill does not take us down

    const time_t deadline = time(nullptr) + timeout_s;
    std::vector<char> buf(8192);
    bool eof_seen = false;
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(out_fds[0], &rfds);
        time_t now = time(nullptr);
        if (now >= deadline) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            res.timed_out = true;
            break;
        }
        timeval tv{0, 100 * 1000}; // 100 ms tick
        int r = select(out_fds[0] + 1, &rfds, nullptr, nullptr, &tv);
        if (r > 0) {
            for (;;) {
                ssize_t n = ::read(out_fds[0], buf.data(), buf.size());
                if (n > 0) { res.out.append(buf.data(), static_cast<size_t>(n)); continue; }
                if (n < 0 && errno == EINTR) continue;
                if (n == 0) eof_seen = true;
                break;
            }
        }
        if (eof_seen) usleep(50 * 1000); // pipe closed but child may still be running
        int status = 0;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            fcntl(out_fds[0], F_SETFL, fcntl(out_fds[0], F_GETFL) | O_NONBLOCK);
            while (true) {
                ssize_t n = ::read(out_fds[0], buf.data(), buf.size());
                if (n > 0) { res.out.append(buf.data(), static_cast<size_t>(n)); continue; }
                break;
            }
            close(out_fds[0]);
            if (WIFEXITED(status)) res.exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) res.exit_code = 128 + WTERMSIG(status);
            return res;
        }
    }

    // timed out: reap
    int status = 0;
    waitpid(pid, &status, 0);
    close(out_fds[0]);
    res.exit_code = res.timed_out ? 124 : res.exit_code;
    return res;
}

void run_detached_terminal(const std::string& shell_cmd, const fs::path& workdir) {
    // terminal candidates per desktop environment
    static const char* terms[][3] = {
        {"x-terminal-emulator", "-e", ""},
        {"xfce4-terminal",      "-x", ""},
        {"gnome-terminal",      "--", ""},
        {"konsole",             "-e", ""},
        {"xterm",               "-e", ""},
    };
    const char* term = nullptr;
    const char* sep = nullptr;
    for (auto& t : terms) {
        fs::path p;
        if (find_in_path(t[0], p)) { term = t[0]; sep = t[1]; break; }
    }

    std::string esc = replace_all(shell_cmd, "'", "'\\''");
    std::string bash_line = "bash -c 'cd \"$PWD\" 2>/dev/null; " + esc + "; echo; echo -- Нажмите Enter для закрытия --; read _'";

    if (!term) {
        // no terminal emulator: fall back to plain detached shell
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", shell_cmd.c_str(), (char*)nullptr);
            _exit(127);
        }
        if (pid > 0) { int st; waitpid(pid, &st, 0); }
        return;
    }

    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        pid_t g = fork(); // double-fork: grandchild is reaped by init
        if (g == 0) {
            setsid();
            if (!workdir.empty()) chdir(workdir.string().c_str());
            execlp(term, term, sep, "/bin/bash", "-c", bash_line.c_str(), (char*)nullptr);
            _exit(127);
        }
        _exit(0);
    }
    int st; waitpid(pid, &st, 0);
}
#endif // !_WIN32

#ifndef _WIN32
void open_url(const std::string& url) {
    if (os() == OS::MacOS) exec_capture({"open", url}, std::nullopt, 5);
    else exec_capture({"xdg-open", url}, std::nullopt, 5);
}
#endif

// ---------- process (Windows) ----------

#ifdef _WIN32
ProcResult exec_capture(const std::vector<std::string>& argv,
                        const std::optional<fs::path>& cwd,
                        int timeout_s,
                        const std::string& stdin_data) {
    ProcResult res;
    if (argv.empty()) return res;

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { res.out = "[exec] CreatePipe failed\n"; return res; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    HANDLE inRd = nullptr, inWr = nullptr;
    const bool has_stdin = !stdin_data.empty();
    if (has_stdin) {
        if (!CreatePipe(&inRd, &inWr, &sa, 0)) { CloseHandle(rd); CloseHandle(wr); res.out = "[exec] CreatePipe failed\n"; return res; }
        SetHandleInformation(inRd, HANDLE_FLAG_INHERIT, 0);
        DWORD written = 0;
        WriteFile(inWr, stdin_data.data(), (DWORD)stdin_data.size(), &written, nullptr);
        // keep inWr open until the child finishes; close before wait loop below
    }

    std::string cmdline;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmdline += " ";
        cmdline += "\"" + replace_all(argv[i], "\"", "\\\"") + "\"";
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = has_stdin ? inRd : GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::string work = cwd ? cwd->string() : std::string();

    BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             work.empty() ? nullptr : work.c_str(), &si, &pi);
    CloseHandle(wr);
    if (has_stdin) CloseHandle(inWr); // child sees EOF after consuming stdin
    if (!ok) {
        CloseHandle(rd);
        if (has_stdin) CloseHandle(inRd);
        res.out = "[exec] CreateProcess failed: " + std::to_string(GetLastError()) + "\n";
        return res;
    }

    const DWORD deadline = GetTickCount() + static_cast<DWORD>(timeout_s) * 1000;
    char buf[8192];
    DWORD n = 0;
    for (;;) {
        if (GetTickCount() > deadline) {
            TerminateProcess(pi.hProcess, 124);
            res.timed_out = true;
        }
        while (PeekNamedPipe(rd, nullptr, 0, nullptr, &n, nullptr) && n > 0) {
            DWORD got = 0;
            if (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got)
                res.out.append(buf, got);
            else break;
        }
        if (WaitForSingleObject(pi.hProcess, 100) == WAIT_OBJECT_0) {
            while (PeekNamedPipe(rd, nullptr, 0, nullptr, &n, nullptr) && n > 0) {
                DWORD got = 0;
                if (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got)
                    res.out.append(buf, got);
                else break;
            }
            break;
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    res.exit_code = static_cast<int>(code);
    CloseHandle(rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (has_stdin) CloseHandle(inRd);
    return res;
}

void run_detached_terminal(const std::string& shell_cmd, const fs::path& workdir) {
    std::string wd = workdir.string();
    std::string args = "/K \"cd /d \"" + wd + "\" && " + shell_cmd + "\"";
    ShellExecuteA(nullptr, nullptr, "cmd.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}
void open_url(const std::string& url) {
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
#endif

// ---------- PATH lookup ----------

bool find_in_path(const std::string& name, fs::path& out) {
    if (name.empty()) return false;
#ifdef _WIN32
    char buf[MAX_PATH];
    if (SearchPathA(nullptr, name.c_str(), ".exe", MAX_PATH, buf, nullptr) > 0) {
        out = buf;
        return true;
    }
    return false;
#else
    if (name.find('/') != std::string::npos) {
        std::error_code ec;
        if (fs::exists(name, ec)) { out = name; return true; }
        return false;
    }
    const char* p = std::getenv("PATH");
    if (!p) return false;
    for (const auto& dir : split(p, ':')) {
        if (dir.empty()) continue;
        fs::path cand = fs::path(dir) / name;
        std::error_code ec;
        if (fs::exists(cand, ec) && !fs::is_directory(cand, ec) &&
            access(cand.string().c_str(), X_OK) == 0) {
            out = cand;
            return true;
        }
    }
    return false;
#endif
}

// ---------- network ----------

bool net_probe(const std::string& host, const std::string& port) {
#ifdef _WIN32
    // WSAStartup handled in main()
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) return false;
    freeaddrinfo(result);
    return true;
#else
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) return false;
    freeaddrinfo(result);
    return true;
#endif
}

int extract_percent(const std::string& chunk) {
    int best = -1;
    for (size_t i = 0; i < chunk.size(); ++i) {
        if (chunk[i] != '%') continue;
        size_t j = i;
        while (j > 0 && isdigit(static_cast<unsigned char>(chunk[j - 1]))) --j;
        if (j < i) {
            int v = std::atoi(chunk.substr(j, i - j).c_str());
            if (v >= 0 && v <= 100) best = v;
        }
    }
    return best;
}

} // namespace util
