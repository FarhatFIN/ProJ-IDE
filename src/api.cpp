// language: C++20, module: api — implementation
#include "api.hpp"
#include "embed.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

namespace api {

using http::Response;

namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path& p, bool& ok) {
    std::ifstream f(p, std::ios::binary);
    ok = bool(f);
    if (!ok) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const fs::path& p, const std::string& content) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return bool(f);
}

bool registered(const Store& store, const fs::path& target) {
    for (const auto& pr : store.projects)
        for (const auto& f : pr.files)
            if (f == target) return true;
    return false;
}

Response json_resp(std::string body, int status = 200) {
    return {status, "application/json; charset=utf-8", std::move(body)};
}

Response err(const std::string& msg, int status = 400) {
    return json_resp(json::obj({{"ok", "false"}, {"error", json::str(msg)}}), status);
}

// ---------- JSON builders ----------

std::string state_json(Context& ctx) {
    std::lock_guard<std::mutex> lk(ctx.mtx);
    std::vector<std::string> pj;
    for (const auto& p : ctx.store.projects) {
        std::vector<std::string> fl;
        for (const auto& f : p.files)
            fl.push_back(json::obj({
                {"path", json::str(util::path_str(f))},
                {"name", json::str(f.filename().string())},
                {"lang", json::str(lang_name(lang_by_ext(f)))},
            }));
        pj.push_back(json::obj({
            {"name", json::str(p.name)},
            {"files", json::arr(fl)},
            {"last", json::str(util::path_str(p.last_open))},
        }));
    }
    return json::obj({
        {"ok", json::boolean(true)},
        {"projects", json::arr(pj)},
    });
}

// ---------- handlers ----------

Response handle_build(const json::Value& body, Context& ctx) {
    const auto* ppath = body.find("path");
    if (!ppath || !ppath->is_string()) return err("path required");
    const fs::path src = ppath->as_string();
    std::string stdin_text;
    if (const auto* si = body.find("stdin"); si && si->is_string()) stdin_text = si->as_string();

    std::lock_guard<std::mutex> lk(ctx.mtx);
    if (!registered(ctx.store, src)) return err("file is not in any project", 403);
    if (!lang_compilable(lang_by_ext(src))) return err("этот тип файла не компилируется");

    const std::string tool = tool_for(lang_by_ext(src));
    std::string ver;
    if (!install::tool_present(tool, ver))
        return json_resp(json::obj({
            {"ok", json::boolean(false)},
            {"needInstall", json::str(tool)},
        }));

    BuildResult r = build_file(src, stdin_text);
    std::vector<std::string> errs;
    for (const auto& e : r.errors)
        errs.push_back(json::obj({
            {"file", json::str(e.file)},
            {"line", json::num(e.line)},
            {"col", json::num(e.col)},
            {"isError", json::boolean(e.is_error)},
            {"msg", json::str(e.msg)},
        }));
    return json_resp(json::obj({
        {"ok", json::boolean(r.ok)},
        {"log", json::str(r.log)},
        {"errors", json::arr(errs)},
        {"errCount", json::num(r.err_count)},
        {"warnCount", json::num(r.warn_count)},
        {"produced", json::str(util::path_str(r.produced))},
    }));
}

Response handle_newproject(const json::Value& body, Context& ctx) {
    const auto* name = body.find("name");
    if (!name || !name->is_string() || name->as_string().empty()) return err("name required");
    const std::string pname = name->as_string();
    const fs::path dir = project_dir(ctx, pname);

    std::lock_guard<std::mutex> lk(ctx.mtx);
    std::vector<fs::path> files;
    if (const auto* fl = body.find("files"); fl && fl->is_array()) {
        for (const auto& f : fl->as_array()) {
            const auto* fn = f.find("name");
            const auto* fc = f.find("content");
            if (!fn || !fn->is_string()) continue;
            std::string safe = fn->as_string();
            if (safe.find('/') != std::string::npos ||
                safe.find('\\') != std::string::npos || safe.find("..") != std::string::npos)
                continue;
            const fs::path fp = dir / safe;
            write_file(fp, fc && fc->is_string() ? fc->as_string() : "");
            files.push_back(fp);
        }
    }
    ctx.store.add_project(pname, files);
    ctx.store.save();
    return json_resp(json::obj({{"ok", json::boolean(true)}}));
}

Response handle_addfiles(const json::Value& body, Context& ctx) {
    const auto* name = body.find("project");
    if (!name || !name->is_string()) return err("project required");
    const auto* fl = body.find("files");
    if (!fl || !fl->is_array()) return err("files required");

    std::lock_guard<std::mutex> lk(ctx.mtx);
    int idx = -1;
    for (int i = 0; i < static_cast<int>(ctx.store.projects.size()); ++i)
        if (ctx.store.projects[i].name == name->as_string()) { idx = i; break; }
    if (idx < 0) return err("no such project", 404);
    const fs::path dir = project_dir(ctx, name->as_string());

    std::vector<fs::path> files;
    for (const auto& f : fl->as_array()) {
        const auto* fn = f.find("name");
        const auto* fc = f.find("content");
        if (!fn || !fn->is_string()) continue;
        std::string safe = fn->as_string();
        if (safe.find('/') != std::string::npos ||
            safe.find('\\') != std::string::npos || safe.find("..") != std::string::npos)
            continue;
        const fs::path fp = dir / safe;
        write_file(fp, fc && fc->is_string() ? fc->as_string() : "");
        files.push_back(fp);
    }
    ctx.store.add_files(idx, files);
    ctx.store.save();
    return json_resp(json::obj({{"ok", json::boolean(true)}}));
}

Response handle_newfile(const json::Value& body, Context& ctx) {
    const auto* name = body.find("project");
    const auto* fname = body.find("name");
    if (!name || !name->is_string() || !fname || !fname->is_string())
        return err("project and name required");
    std::string safe = fname->as_string();
    if (safe.empty() || safe.find('/') != std::string::npos ||
        safe.find('\\') != std::string::npos || safe.find("..") != std::string::npos)
        return err("bad file name");

    std::lock_guard<std::mutex> lk(ctx.mtx);
    int idx = -1;
    for (int i = 0; i < static_cast<int>(ctx.store.projects.size()); ++i)
        if (ctx.store.projects[i].name == name->as_string()) { idx = i; break; }
    if (idx < 0) return err("no such project", 404);
    const fs::path dir = project_dir(ctx, name->as_string());
    const fs::path fp = dir / safe;
    std::error_code ec;
    if (!fs::exists(fp, ec)) write_file(fp, "");
    ctx.store.add_files(idx, {fp});
    ctx.store.save();
    return json_resp(json::obj({
        {"ok", json::boolean(true)},
        {"path", json::str(util::path_str(fp))},
    }));
}

Response handle_deleteproject(const json::Value& body, Context& ctx) {
    const auto* name = body.find("project");
    if (!name || !name->is_string()) return err("project required");
    std::lock_guard<std::mutex> lk(ctx.mtx);
    for (int i = 0; i < static_cast<int>(ctx.store.projects.size()); ++i) {
        if (ctx.store.projects[i].name == name->as_string()) {
            ctx.store.remove_project(i);
            ctx.store.save();
            return json_resp(json::obj({{"ok", json::boolean(true)}}));
        }
    }
    return err("no such project", 404);
}

Response handle_deletefile(const json::Value& body, Context& ctx) {
    const auto* name = body.find("project");
    const auto* pth = body.find("path");
    if (!name || !name->is_string() || !pth || !pth->is_string()) return err("project and path required");
    std::lock_guard<std::mutex> lk(ctx.mtx);
    for (int i = 0; i < static_cast<int>(ctx.store.projects.size()); ++i) {
        if (ctx.store.projects[i].name != name->as_string()) continue;
        ctx.store.remove_file(i, fs::path(pth->as_string()));
        ctx.store.save();
        return json_resp(json::obj({{"ok", json::boolean(true)}}));
    }
    return err("no such project", 404);
}

Response handle_open(const json::Value& body, Context& ctx) {
    const auto* pth = body.find("path");
    if (!pth || !pth->is_string()) return err("path required");
    std::lock_guard<std::mutex> lk(ctx.mtx);
    const fs::path fp = pth->as_string();
    if (!registered(ctx.store, fp)) return err("file is not in any project", 403);
    bool ok = false;
    std::string content = read_file(fp, ok);
    if (!ok) return err("cannot read file", 404);
    // binary guard: NUL in the first 8 KB → not a text file
    const size_t probe = std::min<size_t>(content.size(), 8192);
    if (content.data() != nullptr &&
        std::memchr(content.data(), 0, probe) != nullptr)
        return err("бинарный файл — не открывается как текст");
    for (int i = 0; i < static_cast<int>(ctx.store.projects.size()); ++i)
        for (const auto& f : ctx.store.projects[i].files)
            if (f == fp) { ctx.store.set_last(i, fp); ctx.store.save(); }
    return json_resp(json::obj({
        {"ok", json::boolean(true)},
        {"content", json::str(content)},
        {"name", json::str(fp.filename().string())},
        {"lang", json::str(lang_name(lang_by_ext(fp)))},
    }));
}

Response handle_save(const json::Value& body, Context& ctx) {
    const auto* pth = body.find("path");
    const auto* content = body.find("content");
    if (!pth || !pth->is_string() || !content || !content->is_string())
        return err("path and content required");
    std::lock_guard<std::mutex> lk(ctx.mtx);
    const fs::path fp = pth->as_string();
    if (!registered(ctx.store, fp)) return err("file is not in any project", 403);
    if (!write_file(fp, content->as_string())) return err("cannot write file", 500);
    return json_resp(json::obj({{"ok", json::boolean(true)}}));
}

Response handle_run(const json::Value& body, Context& ctx) {
    const auto* pth = body.find("path");
    if (!pth || !pth->is_string()) return err("path required");
    std::lock_guard<std::mutex> lk(ctx.mtx);
    const fs::path src = pth->as_string();
    if (!registered(ctx.store, src)) return err("file is not in any project", 403);
    const Lang lang = lang_by_ext(src);
    std::string cmd;
    if (lang == Lang::Python) {
        cmd = "python3 '" + util::replace_all(src.string(), "'", "'\\''") + "'";
    } else if (lang == Lang::Cpp || lang == Lang::C || lang == Lang::Asm) {
        fs::path bin = src;
#ifdef _WIN32
        bin.replace_extension(".exe");
#else
        bin.replace_extension("");
#endif
        std::error_code ec;
        if (!fs::exists(bin, ec)) return err("сначала соберите файл", 409);
        cmd = "'" + util::replace_all(bin.string(), "'", "'\\''") + "'";
    } else {
        return err("этот файл нельзя запустить");
    }
    util::run_detached_terminal(cmd, src.parent_path());
    return json_resp(json::obj({{"ok", json::boolean(true)}}));
}

Response handle_runlive(const json::Value& body, Context& ctx) {
    const auto* pth = body.find("path");
    if (!pth || !pth->is_string()) return err("path required");
    std::string produced;
    if (const auto* pr = body.find("produced"); pr && pr->is_string()) produced = pr->as_string();

    std::lock_guard<std::mutex> lk(ctx.mtx);
    const fs::path src = pth->as_string();
    if (!registered(ctx.store, src)) return err("file is not in any project", 403);
    const Lang lang = lang_by_ext(src);
    std::vector<std::string> argv;
    std::string cwd = src.parent_path().string();
    if (lang == Lang::Python) {
        argv = {"python3", "-X", "utf8", src.string()};
    } else if (lang == Lang::Cpp || lang == Lang::C || lang == Lang::Asm) {
        if (produced.empty()) return err("produced required");
        std::error_code ec;
        if (!fs::exists(produced, ec)) return err("сначала соберите файл", 409);
        argv = {produced};
        cwd = fs::path(produced).parent_path().string();
    } else {
        return err("этот файл нельзя запустить");
    }
    if (!ctx.live.start(argv, cwd))
        return err("не удалось запустить процесс");
    return json_resp(json::obj({{"ok", json::boolean(true)}}));
}

Response handle_proc_write(const json::Value& body, Context& ctx) {
    const auto* d = body.find("data");
    if (!d || !d->is_string()) return err("data required");
    if (!ctx.live.write(d->as_string())) return err("процесс не принимает ввод", 409);
    return json_resp(json::obj({{"ok", json::boolean(true)}}));
}

Response handle_proc_read(Context& ctx) {
    std::string chunk;
    const bool running = ctx.live.read_chunk(chunk);
    return json_resp(json::obj({
        {"ok", json::boolean(true)},
        {"running", json::boolean(running)},
        {"chunk", json::str(chunk)},
        {"exitCode", json::num(ctx.live.exit_code())},
    }));
}

Response handle_proc_kill(Context& ctx) {
    ctx.live.kill();
    return json_resp(json::obj({{"ok", json::boolean(true)}}));
}

Response handle_installcheck(const json::Value& body, Context& ctx) {
    (void)ctx;
    const auto* tool = body.find("tool");
    if (!tool || !tool->is_string()) return err("tool required");
    std::string ver;
    const bool present = install::tool_present(tool->as_string(), ver);
    return json_resp(json::obj({
        {"ok", json::boolean(true)},
        {"present", json::boolean(present)},
        {"version", json::str(ver)},
    }));
}

Response handle_install(const json::Value& body, Context& ctx) {
    const auto* tool = body.find("tool");
    if (!tool || !tool->is_string()) return err("tool required");
    if (!ctx.inst_done.load())
        return json_resp(json::obj({{"ok", json::boolean(true)}, {"started", json::boolean(true)}}));
    ctx.inst_tool = tool->as_string();
    ctx.inst_done.store(false);
    ctx.inst_outcome.store(0);
    ctx.inst_percent.store(-1);
    ctx.inst_cancel.store(false);
    std::thread([&ctx]() {
        std::string detail, phase = "Проверка сети...";
        const install::Outcome o = install::install(ctx.inst_tool, ctx.inst_percent,
                                                    ctx.inst_cancel, phase, detail);
        std::lock_guard<std::mutex> lk(ctx.inst_mtx);
        ctx.inst_phase = phase;
        ctx.inst_detail = detail;
        ctx.inst_outcome.store(o == install::Outcome::Ok ? 1 :
                               o == install::Outcome::NetworkError ? 2 : 3);
        ctx.inst_done.store(true);
    }).detach();
    return json_resp(json::obj({{"ok", json::boolean(true)}, {"started", json::boolean(true)}}));
}

Response handle_installstatus(Context& ctx) {
    std::lock_guard<std::mutex> lk(ctx.inst_mtx);
    return json_resp(json::obj({
        {"ok", json::boolean(true)},
        {"done", json::boolean(ctx.inst_done.load())},
        {"outcome", json::num(ctx.inst_outcome.load())},
        {"percent", json::num(ctx.inst_percent.load())},
        {"phase", json::str(ctx.inst_phase)},
        {"detail", json::str(ctx.inst_detail)},
    }));
}

Response handle_static(const std::string& url_path, Context& ctx) {
    std::string rel = url_path == "/" ? "index.html" : url_path.substr(1);
    if (rel.find("..") != std::string::npos)
        return {403, "text/plain", "forbidden"};
    // disk first (development builds), then the payload embedded in the binary
    const fs::path fp = ctx.web_root / rel;
    std::error_code ec;
    if (fs::exists(fp, ec) && !fs::is_directory(fp, ec)) {
        bool ok = false;
        std::string content = read_file(fp, ok);
        if (ok) return {200, http::mime_for(rel), content};
    }
    if (auto blob = embed::get(rel))
        return {200, http::mime_for(rel), *blob};
    return {404, "text/plain", "not found"};
}

} // namespace

fs::path project_dir(const Context& ctx, const std::string& name) {
    std::string safe;
    for (char c : name)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ||
            static_cast<unsigned char>(c) >= 0x80)
            safe += c;
    if (safe.empty()) safe = "project";
    return ctx.projects_root / safe;
}

http::Handler make_handler(Context& ctx) {
    return [&ctx](const http::Request& req) -> http::Response {
        if (req.path.rfind("/api/", 0) == 0) {
            json::Value body;
            if (!req.body.empty()) json::parse(req.body, body);

            if (req.method == "GET" && req.path == "/api/state") return json_resp(state_json(ctx));
            if (req.method == "POST" && req.path == "/api/build") return handle_build(body, ctx);
            if (req.method == "POST" && req.path == "/api/open") return handle_open(body, ctx);
            if (req.method == "POST" && req.path == "/api/save") return handle_save(body, ctx);
            if (req.method == "POST" && req.path == "/api/run") return handle_run(body, ctx);
            if (req.method == "POST" && req.path == "/api/newproject") return handle_newproject(body, ctx);
            if (req.method == "POST" && req.path == "/api/addfiles") return handle_addfiles(body, ctx);
            if (req.method == "POST" && req.path == "/api/newfile") return handle_newfile(body, ctx);
            if (req.method == "POST" && req.path == "/api/deleteproject") return handle_deleteproject(body, ctx);
            if (req.method == "POST" && req.path == "/api/deletefile") return handle_deletefile(body, ctx);
            if (req.method == "POST" && req.path == "/api/installcheck") return handle_installcheck(body, ctx);
            if (req.method == "POST" && req.path == "/api/install") return handle_install(body, ctx);
            if (req.method == "GET" && req.path == "/api/installstatus") return handle_installstatus(ctx);
            if (req.method == "POST" && req.path == "/api/runlive") return handle_runlive(body, ctx);
            if (req.method == "POST" && req.path == "/api/proc/write") return handle_proc_write(body, ctx);
            if (req.method == "GET" && req.path == "/api/proc/read") return handle_proc_read(ctx);
            if (req.method == "POST" && req.path == "/api/proc/kill") return handle_proc_kill(ctx);
            return err("unknown endpoint", 404);
        }
        if (req.method == "GET") return handle_static(req.path, ctx);
        return err("method not allowed", 405);
    };
}

} // namespace api
