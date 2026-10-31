// language: C++20, module: embed — static files stored inside the binary
// The build links a ustar archive of web/ via `ld -r -b binary web.tar`;
// symbols _binary_web_tar_start/_end bracket the payload. Parsed in memory
// (GNU longname entries supported). Disk files still take priority in dev.
#pragma once

#include <optional>
#include <string>

namespace embed {

// path relative to web/, e.g. "index.html", "monaco/vs/loader.js"
std::optional<std::string> get(const std::string& path);

} // namespace embed
