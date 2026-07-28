#include "config.h"

#include <unistd.h>

#include <array>
#include <chrono>
#include <ctime>
#include <fstream>

#include "json.hpp"
#include "paths.h"

using nlohmann::json;

namespace claude_sync {
namespace {

fs::path config_path() { return sync_dir_path() / "config.json"; }
fs::path state_path() { return sync_dir_path() / "state.json"; }

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
    c.encrypted = j.value("encrypted", false);

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
    j["encrypted"] = c.encrypted;
    j["scope"] = {{"memory", c.scope.memory},
                  {"globalClaudeMd", c.scope.globalClaudeMd},
                  {"skills", c.scope.skills},
                  {"agents", c.scope.agents},
                  {"commands", c.scope.commands}};
    j["exclude"] = c.exclude;

    return write_atomic(config_path(), j.dump(2) + "\n");
}

State load_state() {
    State s;

    std::ifstream in(state_path());
    if (!in) return s;

    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return s;

    s.lastSync = j.value("lastSync", std::string{});

    if (j.contains("baseline") && j["baseline"].is_object()) {
        for (const auto& [id, files] : j["baseline"].items()) {
            if (!files.is_object()) continue;
            FileSet fs;
            for (const auto& [name, hash] : files.items()) {
                if (hash.is_string()) fs[name] = hash.get<std::string>();
            }
            s.baseline[id] = std::move(fs);
        }
    }

    if (j.contains("observed") && j["observed"].is_object()) {
        for (const auto& [root, o] : j["observed"].items()) {
            if (!o.is_object()) continue;
            Observation obs;
            obs.remote = o.value("remote", std::string{});
            obs.firstSeen = o.value("firstSeen", std::string{});
            s.observed[root] = std::move(obs);
        }
    }

    if (j.contains("projects") && j["projects"].is_array()) {
        for (const auto& e : j["projects"]) {
            if (!e.is_object()) continue;
            Project p;
            p.escapedDir = e.value("escapedDir", std::string{});
            p.cwd = e.value("cwd", std::string{});
            p.repoRoot = e.value("repoRoot", std::string{});
            p.id = e.value("id", std::string{});
            p.rootCommit = e.value("rootCommit", std::string{});
            p.remoteUrl = e.value("remoteUrl", std::string{});
            s.projects.push_back(std::move(p));
        }
    }

    return s;
}

bool save_state(const State& s) {
    json entries = json::array();
    for (const auto& p : s.projects) {
        if (p.id.empty()) continue;
        entries.push_back({{"escapedDir", p.escapedDir},
                           {"cwd", p.cwd.string()},
                           {"repoRoot", p.repoRoot.string()},
                           {"id", p.id},
                           {"rootCommit", p.rootCommit},
                           {"remoteUrl", p.remoteUrl}});
    }

    json baseline = json::object();
    for (const auto& [id, files] : s.baseline) {
        json f = json::object();
        for (const auto& [name, hash] : files) f[name] = hash;
        baseline[id] = f;
    }

    json observed = json::object();
    for (const auto& [root, o] : s.observed) {
        observed[root] = {{"remote", o.remote}, {"firstSeen", o.firstSeen}};
    }

    json j;
    j["version"] = 3;
    j["lastSync"] = s.lastSync.empty() ? now_iso8601() : s.lastSync;
    j["projects"] = entries;
    j["baseline"] = baseline;
    j["observed"] = observed;

    return write_atomic(state_path(), j.dump(2) + "\n");
}

}  // namespace claude_sync
