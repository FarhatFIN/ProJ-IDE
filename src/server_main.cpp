// language: C++20, file: server_main.cpp — ProJ IDE v3: C++ engine + local web UI
// Serves the UI and JSON API on 127.0.0.1:<ephemeral>, opens the default browser.
#include "http.hpp"
#include "api.hpp"
#include "util.hpp"
#include "lang.hpp"
#include "store.hpp"

#include <csignal>
#include <fstream>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

static api::Context* g_ctx = nullptr;
static void on_sigint(int) {
    if (g_ctx) {
        std::lock_guard<std::mutex> lk(g_ctx->mtx);
        g_ctx->store.set_last(0, fs::path(""));
        g_ctx->store.save();
    }
    std::exit(0);
}

static void open_browser(const std::string& url) {
    util::open_url(url);
}

static void seed_sample(api::Context& ctx) {
    if (!ctx.store.projects.empty()) return;
    const fs::path dir = api::project_dir(ctx, "hello");
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path main_cpp = dir / "main.cpp";
    const fs::path script_py = dir / "script.py";
    if (!fs::exists(main_cpp, ec)) {
        std::ofstream f(main_cpp, std::ios::binary);
        f << "#include <iostream>\n\nint main() {\n"
             "    std::cout << \"Привет, ProJ IDE!\" << std::endl;\n"
             "    return 0;\n}\n";
    }
    if (!fs::exists(script_py, ec)) {
        std::ofstream f(script_py, std::ios::binary);
        f << "name = \"мир\"\nfor i in range(3):\n    print(f\"{i}: привет, {name}\")\n";
    }
    ctx.store.add_project("hello", {main_cpp, script_py});
    ctx.store.save();
}

int main(int argc, char** argv) {
    fs::path self = fs::absolute(fs::path(argv[0])).parent_path();
    fs::path web_root = self / "web";
    if (!fs::exists(web_root / "index.html")) {
        // running from a build dir — fall back to the source tree
        web_root = fs::current_path() / "web";
    }

    api::Context ctx;
    ctx.web_root = web_root;
    ctx.projects_root = util::home_dir() / "ProJ-IDE-Projects";
    std::error_code ec;
    fs::create_directories(ctx.projects_root, ec);
    g_ctx = &ctx;
    std::signal(SIGINT, on_sigint);
    std::signal(SIGTERM, on_sigint);

    ctx.store.load();
    seed_sample(ctx);

    http::Server server;
    if (!server.start(api::make_handler(ctx))) {
        std::fprintf(stderr, "не удалось поднять сервер на 127.0.0.1\n");
        return 1;
    }
    const std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
    std::printf("ProJ IDE: %s\n", url.c_str());
    std::fflush(stdout);

    bool no_browser = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--no-browser") no_browser = true;
    if (!no_browser) open_browser(url);

    server.run_forever();
    return 0;
}
