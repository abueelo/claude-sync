#include "hook.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include "config.h"
#include "crypt.h"
#include "json.hpp"
#include "paths.h"
#include "project.h"
#include "sync.h"

using nlohmann::json;

namespace claude_sync {
namespace {

std::string read_stdin() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

// Newest mtime across a project's memory directory. The Stop hook fires on
// every assistant turn, so the common case has to cost a directory stat and
// nothing else -- no git, no network, no decryption.
long long newest_memory_mtime(const fs::path& memoryDir) {
    long long newest = 0;
    std::error_code ec;
    if (!fs::is_directory(memoryDir, ec)) return 0;

    fs::directory_iterator it(memoryDir, ec);
    if (ec) return 0;
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec)) continue;
        newest = std::max(newest, mtime_seconds(entry.path()));
    }
    return newest;
}

fs::path stamp_path() { return sync_dir_path() / "last-seen.json"; }

// Remembers the newest mtime seen per project directory so an unchanged one
// can be skipped without touching the repo at all.
bool memory_changed(const std::string& escapedDir, const fs::path& memoryDir) {
    long long now = newest_memory_mtime(memoryDir);

    json stamps = json::object();
    std::string text = read_file(stamp_path());
    if (!text.empty()) {
        json parsed = json::parse(text, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) stamps = parsed;
    }

    long long before = stamps.value(escapedDir, 0LL);
    if (before == now) return false;

    stamps[escapedDir] = now;
    write_atomic(stamp_path(), stamps.dump());
    return true;
}

}  // namespace

void log_line(const std::string& message) {
    std::error_code ec;
    fs::create_directories(sync_dir_path(), ec);

    std::ofstream out(sync_dir_path() / "claude-sync.log", std::ios::app);
    if (!out) return;
    out << now_iso8601() << "  " << message << "\n";
}

int run_hook(const std::string& event) {
    std::string payload = read_stdin();

    std::string cwd;
    if (!payload.empty()) {
        json j = json::parse(payload, nullptr, false);
        if (!j.is_discarded() && j.is_object()) cwd = j.value("cwd", std::string{});
    }

    if (!config_exists()) {
        log_line(event + ": not configured yet, nothing to do");
        return 0;
    }

    Config c = load_config();
    if (c.remoteUrl.empty()) {
        log_line(event + ": no remote configured, nothing to do");
        return 0;
    }

    if (c.encrypted && !keys_cached(sync_dir_path())) {
        log_line(event +
                 ": this repo is encrypted and no key is cached; run 'claude-sync unlock' "
                 "once in a terminal to enter the password. Syncing is paused until then.");
        return 0;
    }

    // The Stop hook runs after every assistant turn. Doing a full scan and a
    // git round trip that often would be wasteful and slow, so it exits early
    // unless this project's memory actually changed on disk.
    if (event == "stop" && !cwd.empty()) {
        std::string escaped;
        for (const auto& dir : list_project_dirs()) {
            if (cwd_for_project_dir(dir) == cwd) {
                escaped = dir.filename().string();
                if (!memory_changed(escaped, dir / "memory")) {
                    return 0;
                }
                break;
            }
        }
    }

    SyncOptions opts;
    if (event == "session-start") {
        // Pull before Claude reads memory; pushing here would only delay start.
        opts.fetch = true;
        opts.push = false;
    } else {
        opts.fetch = true;
        opts.push = true;
    }

    SyncReport r = run_sync(opts);

    for (const auto& line : r.log) log_line(event + ": " + line);
    for (const auto& e : r.errors) log_line(event + ": ERROR " + e);

    if (r.errors.empty() && r.stats.changed()) {
        log_line(event + ": " + std::to_string(r.stats.toRepo) + " up, " +
                 std::to_string(r.stats.toLocal) + " down" +
                 (r.stats.conflicts ? ", " + std::to_string(r.stats.conflicts) + " conflicts"
                                    : ""));
    }

    // Always. See the header.
    return 0;
}

}  // namespace claude_sync
