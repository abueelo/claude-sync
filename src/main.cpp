#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "config.h"
#include "git.h"
#include "json.hpp"
#include "paths.h"
#include "project.h"

using nlohmann::json;
using namespace csync;

namespace {

void print_usage() {
    std::cout << "csync - sync Claude Code memory across devices\n\n"
                 "usage:\n"
                 "  csync init [--remote URL]   create ~/.claude/csync and write config.json\n"
                 "  csync status [--json]       resolve every project's identity\n"
                 "  csync help\n";
}

int cmd_init(const std::vector<std::string>& args) {
    std::string remote;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--remote" && i + 1 < args.size()) {
            remote = args[++i];
        } else if (args[i] == "--create-remote") {
            std::cerr << "csync: --create-remote is not wired up yet; it arrives with the\n"
                         "       network code. Nothing has contacted GitHub.\n";
            return 1;
        } else {
            std::cerr << "csync init: unknown argument '" << args[i] << "'\n";
            return 2;
        }
    }

    bool existed = config_exists();
    Config c = existed ? load_config() : default_config();
    if (!remote.empty()) c.remoteUrl = remote;

    if (!save_config(c)) {
        std::cerr << "csync: could not write " << (csync_dir() / "config.json") << "\n";
        return 1;
    }

    std::cout << (existed ? "updated " : "created ") << (csync_dir() / "config.json") << "\n";
    std::cout << "machine: " << c.machineName << "\n";
    std::cout << "remote:  " << (c.remoteUrl.empty() ? "(none set)" : c.remoteUrl) << "\n";
    if (c.remoteUrl.empty()) {
        std::cout << "\nNo sync remote yet. Nothing is pushed or fetched until one is set.\n";
    }
    return 0;
}

std::string display_name(const Project& p) {
    if (!p.cwd.empty()) return p.cwd.filename().string();
    return p.escapedDir;
}

int cmd_status(const std::vector<std::string>& args) {
    bool as_json = false;
    for (const auto& a : args) {
        if (a == "--json") {
            as_json = true;
        } else {
            std::cerr << "csync status: unknown argument '" << a << "'\n";
            return 2;
        }
    }

    std::vector<Project> projects = scan_all();

    if (as_json) {
        json out = json::array();
        for (const auto& p : projects) {
            json e;
            e["escapedDir"] = p.escapedDir;
            e["name"] = display_name(p);
            e["cwd"] = p.cwd.string();
            e["repoRoot"] = p.repoRoot.string();
            e["state"] = state_name(p.state);
            e["id"] = p.id;
            e["rootCommit"] = p.rootCommit;
            e["remoteUrl"] = p.remoteUrl;
            e["memoryFiles"] = p.memoryFileCount;
            if (!p.note.empty()) e["note"] = p.note;
            out.push_back(e);
        }
        std::cout << out.dump(2) << "\n";
    } else {
        size_t idw = 2;
        for (const auto& p : projects) {
            idw = std::max(idw, (p.id.empty() ? std::string("-") : p.id).size());
        }

        std::cout << std::left << std::setw(static_cast<int>(idw) + 2) << "ID" << std::setw(12)
                  << "STATE" << std::setw(22) << "PROJECT" << "MEMORIES\n";

        for (const auto& p : projects) {
            std::cout << std::left << std::setw(static_cast<int>(idw) + 2)
                      << (p.id.empty() ? "-" : p.id) << std::setw(12) << state_name(p.state)
                      << std::setw(22) << display_name(p);
            if (p.memoryFileCount < 0) {
                std::cout << "-";
            } else {
                std::cout << p.memoryFileCount;
            }
            if (!p.note.empty()) std::cout << "   (" << p.note << ")";
            std::cout << "\n";
        }
    }

    if (config_exists()) save_state(projects);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "help" || args[0] == "-h" || args[0] == "--help") {
        print_usage();
        return args.empty() ? 1 : 0;
    }

    std::string cmd = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());

    if (cmd == "init") return cmd_init(rest);
    if (cmd == "status") return cmd_status(rest);

    std::cerr << "csync: unknown command '" << cmd << "'\n\n";
    print_usage();
    return 2;
}
