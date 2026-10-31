// language: C++20, module: http — implementation
#include "http.hpp"

#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#  define CLOSE_SOCK(s) closesocket(s)
#  define SOCK_ERR WSAGetLastError()
#else
#  include <unistd.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/socket.h>
#  define CLOSE_SOCK(s) close(s)
#  define SOCK_ERR errno
#endif

namespace http {

const char* mime_for(const std::string& path) {
    const auto ends = [&](const char* e) {
        const size_t n = std::strlen(e);
        return path.size() >= n && path.compare(path.size() - n, n, e) == 0;
    };
    if (ends(".html") || ends(".htm")) return "text/html; charset=utf-8";
    if (ends(".js"))   return "text/javascript; charset=utf-8";
    if (ends(".mjs"))  return "text/javascript; charset=utf-8";
    if (ends(".css"))  return "text/css; charset=utf-8";
    if (ends(".json")) return "application/json";
    if (ends(".svg"))  return "image/svg+xml";
    if (ends(".png"))  return "image/png";
    if (ends(".ico"))  return "image/x-icon";
    if (ends(".woff")) return "font/woff";
    if (ends(".woff2")) return "font/woff2";
    if (ends(".ttf"))  return "font/ttf";
    if (ends(".wasm")) return "application/wasm";
    if (ends(".map"))  return "application/json";
    return "application/octet-stream";
}

namespace {

// reads until \r\n\r\n, then Content-Length body bytes
bool read_request(int fd, Request& req) {
    std::string buf;
    char chunk[8192];
    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
#ifdef _WIN32
        if (n <= 0) return false;
#else
        if (n <= 0) return false;
#endif
        buf.append(chunk, static_cast<size_t>(n));
        header_end = buf.find("\r\n\r\n");
        if (buf.size() > 1024 * 1024) return false; // header flood guard
    }
    const std::string head = buf.substr(0, header_end);
    std::string rest = buf.substr(header_end + 4);

    size_t sp1 = head.find(' ');
    size_t sp2 = head.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
    req.method = head.substr(0, sp1);
    req.path = head.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t q = req.path.find('?');
    if (q != std::string::npos) req.path.resize(q);

    size_t len = 0;
    size_t pos = head.find("Content-Length:");
    if (pos == std::string::npos) pos = head.find("content-length:");
    if (pos != std::string::npos) len = static_cast<size_t>(std::atoll(head.c_str() + pos + 15));

    while (rest.size() < len) {
        ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        rest.append(chunk, static_cast<size_t>(n));
    }
    req.body = rest.substr(0, len);
    return true;
}

bool write_all(int fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::send(fd, data + off, len - off, 0);
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

void send_response(int fd, const Response& r) {
    std::string head = "HTTP/1.1 " + std::to_string(r.status) + " " +
                       (r.status == 200 ? "OK" : "ERR") + "\r\n";
    head += "Content-Type: " + r.content_type + "\r\n";
    head += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
    head += "Cache-Control: no-cache\r\n";
    head += "Connection: keep-alive\r\n\r\n";
    if (!write_all(fd, head.data(), head.size())) return;
    if (!r.body.empty()) write_all(fd, r.body.data(), r.body.size());
}

void connection_loop(int fd, const Handler& handler) {
    for (int i = 0; i < 1000; ++i) { // keep-alive budget per connection
        Request req;
        if (!read_request(fd, req)) break;
        Response res;
        try {
            res = handler(req);
        } catch (const std::exception& e) {
            res = {500, "application/json; charset=utf-8",
                   std::string("{\"error\":\"") + e.what() + "\"}"};
        } catch (...) {
            res = {500, "application/json; charset=utf-8", "{\"error\":\"internal\"}"};
        }
        send_response(fd, res);
        if (req.method == "POST" && req.path.rfind("/api/build", 0) == 0) {
            // long ops stay keep-alive; nothing special needed
        }
    }
    CLOSE_SOCK(fd);
}

} // namespace

bool Server::start(Handler handler) {
    handler_ = std::move(handler);
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    listen_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (listen_fd_ < 0) return false;
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&one), sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 only
    addr.sin_port = 0;                             // ephemeral
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    socklen_t alen = sizeof(addr);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &alen) != 0) return false;
    port_ = ntohs(addr.sin_port);
    if (::listen(listen_fd_, 16) != 0) return false;
    return true;
}

void Server::run_forever() {
    while (true) {
        int fd = static_cast<int>(::accept(listen_fd_, nullptr, nullptr));
        if (fd < 0) continue;
        std::thread(connection_loop, fd, handler_).detach();
    }
}

} // namespace http
