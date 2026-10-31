// language: C++20, module: api — request routing over the engine (store/build/install)
#pragma once

#include "http.hpp"
#include "jsonlite.hpp"
#include "store.hpp"
#include "build.hpp"
#include "lang.hpp"
#include "install.hpp"
#include "util.hpp"
#include "liveproc.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace api {

struct Context {
    Store store;
    std::mutex mtx;                      // guards store + engine ops that touch it
    fs::path web_root;                   // directory with index.html
    fs::path projects_root;              // ~/ProJ-IDE-Projects

    liveproc::LiveProc live;             // interactive program attached to the UI

    // installer state (worker thread + pollers)
    std::atomic<int> inst_percent{-1};
    std::atomic<bool> inst_done{true};
    std::atomic<int> inst_outcome{0};    // 0 idle/failed-start, 1 ok, 2 neterr, 3 failed
    std::string inst_tool, inst_phase, inst_detail;
    std::atomic<bool> inst_cancel{false};
    std::mutex inst_mtx;
};

// binds context; returns the full request handler for http::Server
http::Handler make_handler(Context& ctx);

// helper used by newproject/newfile: default storage dir for a project
fs::path project_dir(const Context& ctx, const std::string& name);

} // namespace api
