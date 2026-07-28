#include "paths.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace csync {
namespace {

// Pulls the value of the first "cwd" field out of a transcript. These files run
// to tens of megabytes, and the field sits in the opening record, so this reads
// a bounded prefix and scans it as text rather than parsing every line as JSON.
std::string scan_cwd(const fs::path& jsonl) {
    constexpr size_t kMaxScan = 256 * 1024;

    std::ifstream in(jsonl, std::ios::binary);
    if (!in) return {};

    std::string buf(kMaxScan, '\0');
    in.read(&buf[0], static_cast<std::streamsize>(kMaxScan));
    buf.resize(static_cast<size_t>(in.gcount()));

    const std::string key = "\"cwd\":\"";
    size_t pos = buf.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();

    std::string value;
    for (size_t i = pos; i < buf.size(); ++i) {
        char c = buf[i];
        if (c == '"') return value;
        if (c == '\\' && i + 1 < buf.size()) {
            char next = buf[++i];
            switch (next) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                case '/': value += '/'; break;
                default: value += next; break;
            }
            continue;
        }
        value += c;
    }
    return {};  // ran off the end of the scanned prefix without closing
}

}  // namespace

fs::path home_dir() {
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h);
    return {};
}

fs::path claude_dir() {
    if (const char* o = std::getenv("CSYNC_CLAUDE_DIR"); o && *o) return fs::path(o);
    return home_dir() / ".claude";
}

fs::path projects_dir() { return claude_dir() / "projects"; }

fs::path csync_dir() { return claude_dir() / "csync"; }

std::vector<fs::path> list_project_dirs() {
    std::vector<fs::path> dirs;
    std::error_code ec;

    fs::directory_iterator it(projects_dir(), ec);
    if (ec) return dirs;

    for (const auto& entry : it) {
        if (entry.is_directory(ec)) dirs.push_back(entry.path());
    }
    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

std::string cwd_for_project_dir(const fs::path& project_dir) {
    std::vector<fs::path> transcripts;
    std::error_code ec;

    fs::directory_iterator it(project_dir, ec);
    if (ec) return {};

    for (const auto& entry : it) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".jsonl") {
            transcripts.push_back(entry.path());
        }
    }
    if (transcripts.empty()) return {};

    // Newest first: the most recent session reflects where the project is now.
    std::sort(transcripts.begin(), transcripts.end(), [](const fs::path& a, const fs::path& b) {
        std::error_code e1, e2;
        auto ta = fs::last_write_time(a, e1);
        auto tb = fs::last_write_time(b, e2);
        return ta > tb;
    });

    for (const auto& t : transcripts) {
        std::string cwd = scan_cwd(t);
        if (!cwd.empty()) return cwd;
    }
    return {};
}

int count_memory_files(const fs::path& project_dir) {
    std::error_code ec;
    fs::path mem = project_dir / "memory";
    if (!fs::is_directory(mem, ec)) return -1;

    int n = 0;
    fs::directory_iterator it(mem, ec);
    if (ec) return -1;
    for (const auto& entry : it) {
        if (entry.is_regular_file(ec)) ++n;
    }
    return n;
}

bool write_atomic(const fs::path& target, const std::string& contents) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);

    fs::path tmp = target;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!out) return false;
    }

    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace csync
