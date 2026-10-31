// language: C++20, module: liveproc — POSIX implementation
#include "liveproc.hpp"

#include <utility>
#include <vector>

#include "util.hpp"
#include <filesystem>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <unistd.h>
#  include <errno.h>
#  include <signal.h>
#  include <sys/wait.h>
#  include <fcntl.h>
#  include <thread>
#endif

namespace liveproc {

#ifndef _WIN32

namespace {

constexpr size_t kOutCap = 512 * 1024; // keep the tail of chatty programs

void write_all_fd(int fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        off += static_cast<size_t>(n);
    }
}

} // namespace

bool LiveProc::start(const std::vector<std::string>& argv, const std::string& cwd) {
    std::lock_guard<std::mutex> lk(m_);
    if (running_) {
        ::kill(-pid_, SIGKILL);
        ::kill(pid_, SIGKILL);
        if (in_fd_ >= 0) { ::close(in_fd_); in_fd_ = -1; }
    }

    int outp[2], inp[2];
    if (pipe(outp) != 0 || pipe(inp) != 0) return false;

    // stdio through a pipe is block-buffered — the child's prompts would sit
    // in its buffer until exit. stdbuf forces line buffering (libc programs).
    std::vector<std::string> av = argv;
    {
        fs::path stdbuf;
        std::error_code ec;
        if (util::find_in_path("stdbuf", stdbuf) && fs::exists(argv[0], ec)) {
            av.insert(av.begin(), {"-oL", "-eL"});
            av.insert(av.begin(), stdbuf.string());
        }
    }

    std::vector<char*> args;
    args.reserve(av.size() + 1);
    for (const auto& a : av) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        ::close(outp[0]); ::close(outp[1]);
        ::close(inp[0]);  ::close(inp[1]);
        return false;
    }
    if (pid == 0) {
        setpgid(0, 0);
        if (!cwd.empty()) chdir(cwd.c_str());
        dup2(outp[1], STDOUT_FILENO);
        dup2(outp[1], STDERR_FILENO);
        dup2(inp[0], STDIN_FILENO);
        ::close(outp[0]); ::close(outp[1]);
        ::close(inp[0]);  ::close(inp[1]);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        execvp(args[0], args.data());
        _exit(127);
    }

    ::close(outp[1]);
    ::close(inp[0]);
    setpgid(pid, pid);

    pid_ = pid;
    in_fd_ = inp[1];
    out_fd_ = outp[0];
    exit_code_ = 0;
    outbuf_.clear();
    running_ = true;
    reader_alive_ = true;

    // reader: accumulates output until EOF, then reaps the child
    std::thread([this, pid, outfd = out_fd_]() {
        char buf[8192];
        for (;;) {
            ssize_t n = ::read(outfd, buf, sizeof(buf));
            if (n > 0) {
                std::lock_guard<std::mutex> lk(m_);
                outbuf_.append(buf, static_cast<size_t>(n));
                if (outbuf_.size() > kOutCap)
                    outbuf_.erase(0, outbuf_.size() - kOutCap);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        ::close(outfd);
        int st = 0;
        waitpid(pid, &st, 0);
        std::lock_guard<std::mutex> lk(m_);
        if (WIFEXITED(st)) exit_code_ = WEXITSTATUS(st);
        else if (WIFSIGNALED(st)) exit_code_ = 128 + WTERMSIG(st);
        running_ = false;
        reader_alive_ = false;
    }).detach();

    return true;
}

bool LiveProc::write(const std::string& data) {
    std::lock_guard<std::mutex> lk(m_);
    if (!running_ || in_fd_ < 0 || data.empty()) return false;
    write_all_fd(in_fd_, data.data(), data.size());
    return true;
}

bool LiveProc::read_chunk(std::string& chunk) {
    std::lock_guard<std::mutex> lk(m_);
    chunk = std::move(outbuf_);
    outbuf_.clear();
    return running_;
}

void LiveProc::kill() {
    std::lock_guard<std::mutex> lk(m_);
    if (!running_) return;
    ::kill(-pid_, SIGKILL);
    ::kill(pid_, SIGKILL);
    if (in_fd_ >= 0) { ::close(in_fd_); in_fd_ = -1; }
}

int LiveProc::exit_code() {
    std::lock_guard<std::mutex> lk(m_);
    return exit_code_;
}

bool LiveProc::running() {
    std::lock_guard<std::mutex> lk(m_);
    return running_;
}

#else // _WIN32 — honest stub for this build

bool LiveProc::start(const std::vector<std::string>&, const std::string&) { return false; }
bool LiveProc::write(const std::string&) { return false; }
bool LiveProc::read_chunk(std::string&) { return false; }
void LiveProc::kill() {}
int LiveProc::exit_code() { return 0; }
bool LiveProc::running() { return false; }

#endif

} // namespace liveproc
