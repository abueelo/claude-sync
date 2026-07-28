#include "config.h"

#include <unistd.h>

#include <array>
#include <chrono>
#include <ctime>
#include <fstream>

#include "json.hpp"
#include "paths.h"

using nlohmann::json;

namespace csync {
namespace {

fs::path config_path() { return csync_dir() / "config.json"; }
fs::path state_path() { return csync_dir() / "state.json"; }

std::string now_iso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    ::gmtime_r(&t, &tm);
    std::array<char, 32> buf{};
    std::strftime(buf.data(), buf.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf.data();
}

}  // namespace

std::string hostname() {
    std::array<char, 256> buf{};
    if (::gethostname(buf.data(), buf.size() - 1) != 0) return "unknown";
    std::string h = buf.data();
    // Strip a trailing .local / .lan so the name reads like a machine, not a host.
    if (size_t dot = h.find('.'); dot != std::string::npos) h = h.substr(0, dot);
    return h.empty() ? "unknown" : h;
}

Config default_config() {
    Config c;
    c.machineName = hostname();
    return c;
}

bool config_exists() {
    std::error_code ec;
    return fs::exists(config_path(), ec);
}

Config load_config() {
    Config c = default_config();

    std::ifstream in(config_path());
    if (!in) return c;

    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return c;

    c.machineName = j.value("machineName", c.machineName);
    c.remoteUrl = j.value("remoteUrl", std::string{});

    if (j.contains("scope") && j["scope"].is_object()) {
        const auto& s = j["scope"];
        c.scope.memory = s.value("memory", true);
        c.scope.globalClaudeMd = s.value("globalClaudeMd", true);
        c.scope.skills = s.value("skills", true);
        c.scope.agents = s.value("agents", true);
        c.scope.commands = s.value("commands", true);
    }

    if (j.contains("exclude") && j["exclude"].is_array()) {
        for (const auto& e : j["exclude"]) {
            if (e.is_string()) c.exclude.push_back(e.get<std::string>());
        }
    }

    return c;
}

bool save_config(const Config& c) {
    json j;
    j["machineName"] = c.machineName;
    j["remoteUrl"] = c.remoteUrl;
    j["scope"] = {{"memory", c.scope.memory},
                  {"globalClaudeMd", c.scope.globalClaudeMd},
                  {"skills", c.scope.skills},
                  {"agents", c.scope.agents},
                  {"commands", c.scope.commands}};
    j["exclude"] = c.exclude;

    return write_atomic(config_path(), j.dump(2) + "\n");
}

bool save_state(const std::vector<Project>& projects) {
    json entries = json::array();
    for (const auto& p : projects) {
        if (!p.synced()) continue;
        entries.push_back({{"escapedDir", p.escapedDir},
                           {"cwd", p.cwd.string()},
                           {"repoRoot", p.repoRoot.string()},
                           {"id", p.id},
                           {"rootCommit", p.rootCommit},
                           {"remoteUrl", p.remoteUrl}});
    }

    json j;
    j["version"] = 1;
    j["lastScan"] = now_iso8601();
    j["projects"] = entries;

    return write_atomic(state_path(), j.dump(2) + "\n");
}

}  // namespace csync
