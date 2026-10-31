// language: C++20, module: lang — implementation
#include "lang.hpp"
#include "util.hpp"

#include <string>
#include <fstream>

Lang lang_by_ext(const fs::path& p) {
    std::string e = util::to_lower(p.extension().string());
    if (e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".c++" ||
        e == ".hpp" || e == ".hh" || e == ".h" || e == ".hxx" || e == ".ipp" || e == ".tpp")
        return Lang::Cpp;
    if (e == ".c") return Lang::C;
    if (e == ".py" || e == ".pyw" || e == ".pyi") return Lang::Python;
    if (e == ".asm" || e == ".nasm" || e == ".s" || e == ".S") return Lang::Asm;
    if (e == ".md" || e == ".markdown") return Lang::Markdown;
    if (e == ".txt") return Lang::Text;
    // extensionless scripts often carry a shebang; peek at the first line
    if (e.empty() || e == ".sh") {
        std::error_code ec;
        if (fs::exists(p, ec) && fs::file_size(p, ec) > 2) {
            std::ifstream f(p, std::ios::binary);
            if (f) {
                char b[64] = {0};
                f.read(b, sizeof(b) - 1);
                std::string head(b);
                if (util::starts_with(head, "#!")) {
                    if (head.find("python") != std::string::npos) return Lang::Python;
                    if (head.find("bash") != std::string::npos ||
                        head.find("/sh") != std::string::npos) return Lang::Text; // shell: not compilable here
                }
            }
        }
        return Lang::Text;
    }
    return Lang::Unknown;
}

const char* lang_name(Lang l) {
    switch (l) {
    case Lang::Cpp:      return "C++";
    case Lang::C:        return "C";
    case Lang::Python:   return "Python";
    case Lang::Asm:      return "Ассемблер (NASM)";
    case Lang::Markdown: return "Markdown";
    case Lang::Text:     return "Текст";
    default:             return "Неизвестный тип";
    }
}

const char* lang_label(Lang l) {
    switch (l) {
    case Lang::Cpp:      return "C++";
    case Lang::C:        return "C";
    case Lang::Python:   return "PY";
    case Lang::Asm:      return "ASM";
    case Lang::Markdown: return "MD";
    case Lang::Text:     return "TXT";
    default:             return "BIN";
    }
}

const char* tool_for(Lang l) {
    switch (l) {
    case Lang::Cpp:    return "g++";
    case Lang::C:      return "gcc";
    case Lang::Python: return "python3";
    case Lang::Asm:    return "nasm";
    default:           return "";
    }
}

bool lang_compilable(Lang l) {
    return l == Lang::Cpp || l == Lang::C || l == Lang::Python || l == Lang::Asm;
}

namespace trusted {

std::string pkg_debian(const std::string& tool) {
    if (tool == "g++") return "g++";
    if (tool == "gcc") return "gcc";
    if (tool == "python3") return "python3";
    if (tool == "nasm") return "nasm";
    return {};
}

std::string pkg_fedora(const std::string& tool) {
    if (tool == "g++") return "gcc-c++";
    if (tool == "gcc") return "gcc";
    if (tool == "python3") return "python3";
    if (tool == "nasm") return "nasm";
    return {};
}

std::string winget_id(const std::string& tool) {
    if (tool == "g++" || tool == "gcc") return "BrechtSanders.WinLibs.POSIX.UCRT";
    if (tool == "python3") return "Python.Python.3.12";
    if (tool == "nasm") return "NASM.NASM";
    return {};
}

std::string direct_url_windows(const std::string& tool) {
    // Pinned releases of the respective official distributors.
    if (tool == "g++" || tool == "gcc")
        return "https://github.com/brechtsanders/winlibs_mingw/releases/download/"
               "15.1.0posix-13.0.0-ucrt-r4/winlibs-x86_64-posix-seh-gcc-15.1.0-llvm-20.1.5-"
               "mingw-w64ucrt-13.0.0-r4.zip";
    if (tool == "python3")
        return "https://www.python.org/ftp/python/3.12.8/python-3.12.8-amd64.exe";
    if (tool == "nasm")
        return "https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-installer-x64.exe";
    return {};
}

std::string direct_url_macos(const std::string& tool) {
    if (tool == "g++" || tool == "gcc") return {}; // clang via xcode-select --install, no URL needed
    if (tool == "python3")
        return "https://www.python.org/ftp/python/3.12.8/python-3.12.8-macos11.pkg";
    if (tool == "nasm")
        return "https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/macosx/nasm-2.16.03-macosx.zip";
    return {};
}

const char* probe_host() {
    switch (util::os()) {
    case util::OS::Linux: {
        // pick the probe by detected family: debian vs fedora
        std::error_code ec;
        if (fs::exists("/etc/fedora-release", ec)) return "dl.fedoraproject.org";
        return "deb.debian.org";
    }
    default:
        return "www.python.org";
    }
}

} // namespace trusted
