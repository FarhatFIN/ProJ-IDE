// language: C++20, module: build — implementation
#include "build.hpp"
#include "lang.hpp"
#include "util.hpp"

#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>

// gcc:      path:line:col: severity: message   |  path:line: severity: message
// nasm:     path:line: error: message
// python:     File "path", line N              +  "SomeError: message"
std::vector<BuildError> parse_compiler_log(const std::string& log) {
    std::vector<BuildError> out;
    std::istringstream ss(log);
    std::string line;

    BuildError pending_py{}; // python: File-line first, message follows

    auto is_sev_word = [](const std::string& w) {
        return w == "error" || w == "fatal error" || w == "warning" || w == "note";
    };

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // ---- python traceback ----
        {
            const std::string tag = "  File \"";
            size_t t = line.find(tag);
            if (t != std::string::npos) {
                size_t q1 = line.find('"', t + tag.size());
                size_t linepos = line.find(", line ", q1);
                if (q1 != std::string::npos && linepos != std::string::npos) {
                    BuildError e;
                    e.file = line.substr(t + tag.size(), q1 - (t + tag.size()));
                    e.line = std::atoi(line.c_str() + linepos + 7);
                    e.is_error = true;
                    pending_py = e;
                }
                continue;
            }
            // message line of a traceback: non-indented "XError: ..." right after a File line
            if (pending_py.line > 0 && !std::isspace(static_cast<unsigned char>(line[0]))) {
                if (line.find("Error") != std::string::npos ||
                    line.find("Exception") != std::string::npos) {
                    pending_py.msg = line;
                    out.push_back(pending_py);
                }
                pending_py = BuildError{};
                continue;
            }
        }

        // ---- gcc / nasm style ----
        // path : line [: col] : severity : message
        std::string path;
        size_t pos = 0;
        // Windows paths contain ':' (C:\...); colon-scan must skip drive letters there
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
#ifdef _WIN32
        if (colon == 1) colon = line.find(':', colon + 1);
#endif
        path = line.substr(0, colon);
        pos = colon + 1;
        if (path.empty() || path[0] == ' ') continue;

        auto read_int_field = [&](int& val, size_t& p) -> bool {
            size_t s = p;
            while (p < line.size() && isdigit(static_cast<unsigned char>(line[p]))) ++p;
            if (p == s) return false;
            val = std::atoi(line.substr(s, p - s).c_str());
            if (p < line.size() && line[p] == ':') { ++p; return true; }
            return false;
        };

        int ln = 0, col = 0;
        if (!read_int_field(ln, pos)) continue;
        // optional column: if the next field is not "number:" it is the severity
        size_t save = pos;
        if (!read_int_field(col, pos)) { pos = save; col = 0; }

        std::string rest = util::trim(line.substr(pos));
        // rest = "severity: message"
        size_t sc = rest.find(':');
        if (sc == std::string::npos) continue;
        std::string sev = util::to_lower(util::trim(rest.substr(0, sc)));
        if (!is_sev_word(sev)) continue;

        BuildError e;
        e.file = path;
        e.line = ln;
        e.col = col;
        e.is_error = (sev == "error" || sev == "fatal error");
        e.msg = util::trim(rest.substr(sc + 1));
        if (e.msg.empty()) e.msg = "(без сообщения)";
        out.push_back(e);
    }
    return out;
}

static fs::path binary_path_for(const fs::path& src) {
    fs::path out = src;
    out.replace_extension(
#ifdef _WIN32
        ".exe"
#else
        ""
#endif
    );
    return out;
}

BuildResult build_file(const fs::path& src, const std::string& stdin_data, bool debug) {
    BuildResult res;
    const Lang lang = lang_by_ext(src);
    const fs::path dir = src.parent_path();

    auto run_cmd = [&](std::vector<std::string> argv) {
#ifndef _WIN32
        argv.insert(argv.begin(), {"env", "LC_ALL=C"}); // stable english diagnostics
#endif
        return util::exec_capture(argv, dir, 30, stdin_data);
    };

    auto finish = [&](const util::ProcResult& r, const std::string& stage) {
        res.log += r.out;
        if (r.timed_out)
            res.log += "\n[Превышено время ожидания 30 с — процесс остановлен]\n";
        res.errors = parse_compiler_log(res.log);
        res.err_count = 0;
        res.warn_count = 0;
        for (const auto& e : res.errors) (e.is_error ? res.err_count++ : res.warn_count++);
        res.ok = (r.exit_code == 0) && !r.timed_out;
        if (res.ok && stage == "compile") res.produced = binary_path_for(src);
        return res.ok;
    };

    fs::path tool;
    switch (lang) {
    case Lang::Cpp: {
        if (!util::find_in_path("g++", tool)) { res.log = "[нет компилятора g++]"; return res; }
        std::vector<std::string> args = {tool.string(), "-std=c++20", "-Wall", "-Wextra",
                                         "-fdiagnostics-color=never"};
        if (debug) { args.push_back("-g"); args.push_back("-O0"); }
        args.push_back("-o");
        args.push_back(binary_path_for(src).string());
        args.push_back(src.filename().string());
        auto r = run_cmd(args);
        finish(r, "compile");
        return res;
    }
    case Lang::C: {
        if (!util::find_in_path("gcc", tool)) { res.log = "[нет компилятора gcc]"; return res; }
        std::vector<std::string> args = {tool.string(), "-std=c17", "-Wall", "-Wextra",
                                         "-fdiagnostics-color=never"};
        if (debug) { args.push_back("-g"); args.push_back("-O0"); }
        args.push_back("-o");
        args.push_back(binary_path_for(src).string());
        args.push_back(src.filename().string());
        auto r = run_cmd(args);
        finish(r, "compile");
        return res;
    }
    case Lang::Asm: {
        if (!util::find_in_path("nasm", tool)) { res.log = "[нет ассемблера nasm]"; return res; }
        fs::path obj = src; obj.replace_extension(".o");
        const char* fmt =
            util::os() == util::OS::Windows ? "win64" :
            util::os() == util::OS::MacOS   ? "macho64" : "elf64";
        auto r = run_cmd({tool.string(), "-f", fmt, "-o", obj.filename().string(),
                          src.filename().string()});
        if (!finish(r, "asm")) return res;
        if (!util::find_in_path("gcc", tool)) {
            res.log += "\n[нет компилятора gcc для линковки объектного файла]";
            res.ok = false;
            return res;
        }
        auto r2 = run_cmd({tool.string(), obj.filename().string(), "-o",
                           binary_path_for(src).filename().string()});
        finish(r2, "compile");
        return res;
    }
    case Lang::Python: {
        if (!util::find_in_path("python3", tool)) { res.log = "[нет python3]"; return res; }
        // syntax check only — execution happens in the live runner
        auto r = run_cmd({tool.string(), "-m", "py_compile", src.filename().string()});
        finish(r, "check");
        return res;
    }
    default:
        res.log = "[этот тип файла не компилируется]";
        return res;
    }
}

std::vector<BuildError> lint_file(const fs::path& src, const std::string& content) {
    const Lang lang = lang_by_ext(src);
    if (lang != Lang::Cpp && lang != Lang::C && lang != Lang::Python) return {};
    // temp file sits next to the original so relative #include keeps working
    fs::path tmp = src.parent_path() /
                   ("." + src.filename().string() + ".lint" + src.extension().string());
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!f) return {};
    }
    std::vector<std::string> argv;
    if (lang == Lang::Python)
        argv = {"python3", "-m", "py_compile", tmp.string()};
    else
        argv = {lang == Lang::Cpp ? "g++" : "gcc",
                std::string("-std=") + (lang == Lang::Cpp ? "c++20" : "c17"),
                "-fsyntax-only", "-Wall", "-fdiagnostics-color=never", tmp.string()};
#ifndef _WIN32
    argv.insert(argv.begin(), {"env", "LC_ALL=C"});
#endif
    auto r = util::exec_capture(argv, src.parent_path(), 20);
    std::error_code ec;
    fs::remove(tmp, ec);
    auto diags = parse_compiler_log(r.out);
    const std::string tmpname = tmp.filename().string();
    for (auto& e : diags)
        if (!e.file.empty() && fs::path(e.file).filename() == tmpname)
            e.file = src.string();
    return diags;
}
