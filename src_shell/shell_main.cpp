// language: C++20, file: shell_main.cpp — native desktop shell for ProJ IDE
// GTK3 window + WebKitWebView pointing at the embedded C++ engine server.
// One process = one program: window close shuts everything down.
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include "http.hpp"
#include "api.hpp"
#include "util.hpp"
#include "lang.hpp"
#include "store.hpp"

#include <fstream>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

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

static void on_destroy(GtkWidget*, gpointer) { gtk_main_quit(); }

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);

    fs::path self = fs::absolute(fs::path(argv[0])).parent_path();
    fs::path web_root = self / "web";
    if (!fs::exists(web_root / "index.html"))
        web_root = fs::current_path() / "web";

    api::Context ctx;
    ctx.web_root = web_root;
    ctx.projects_root = util::home_dir() / "ProJ-IDE-Projects";
    std::error_code ec;
    fs::create_directories(ctx.projects_root, ec);

    ctx.store.load();
    seed_sample(ctx);

    http::Server server;
    if (!server.start(api::make_handler(ctx))) {
        g_error("не удалось поднять сервер");
        return 1;
    }
    const std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
    std::printf("ProJ IDE: %s\n", url.c_str());
    std::fflush(stdout);
    std::thread([&server]() { server.run_forever(); }).detach();

    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "ProJ IDE");
    gtk_window_set_default_size(GTK_WINDOW(win), 1280, 820);
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), nullptr);

    GtkWidget* view = webkit_web_view_new();
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), url.c_str());
    gtk_container_add(GTK_CONTAINER(win), view);
    gtk_widget_grab_focus(view);
    gtk_widget_show_all(win);

    gtk_main();
    return 0;
}
