// language: C++20, module: http — minimal HTTP/1.1 server for the local UI
// Binds 127.0.0.1 only. Thread per connection, keep-alive, Content-Length bodies.
#pragma once

#include <functional>
#include <string>

namespace http {

struct Request {
    std::string method;   // GET / POST
    std::string path;     // /api/state (no query)
    std::string body;
};

struct Response {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
};

using Handler = std::function<Response(const Request&)>;

class Server {
public:
    // handler must be thread-safe (called from connection threads)
    bool start(Handler handler);
    int port() const { return port_; }
    void run_forever();          // accept loop, blocks
private:
    Handler handler_;
    int listen_fd_ = -1;
    int port_ = 0;
};

// mime type by extension for the static handler
const char* mime_for(const std::string& path);

} // namespace http
