// language: C++20, module: liveproc — one interactive child process with live stdin/stdout
// POSIX implementation (fork/execvp/pipes). Windows: honest stub.
#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace liveproc {

class LiveProc {
public:
    // Kills any running instance, then spawns argv with merged stdout+stderr.
    bool start(const std::vector<std::string>& argv, const std::string& cwd);

    // Writes raw data to the child's stdin.
    bool write(const std::string& data);

    // Moves newly accumulated output into `chunk`. Returns true while running.
    bool read_chunk(std::string& chunk);

    // Sends SIGKILL. Safe when nothing is running.
    void kill();

    int exit_code();
    bool running();

private:
    void cleanup_locked();

    std::mutex m_;
    bool running_ = false;
    int exit_code_ = 0;
    int pid_ = -1;
    int in_fd_ = -1;    // child stdin
    int out_fd_ = -1;   // child stdout+stderr
    std::string outbuf_;
    bool reader_alive_ = false;
};

} // namespace liveproc
