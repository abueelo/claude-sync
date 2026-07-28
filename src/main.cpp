#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "config.h"
#include "git.h"
#include "json.hpp"
#include "paths.h"
#include "project.h"
#include "repo.h"
#include "sync.h"

using nlohmann::json;
using namespace csync;

namespace {

void print_usage() {
    std::cout << "csync - sync Claude Code memory across devices\n\n"
                 "usage:\n"
                 "  csync init [--remote URL] [--create-remote NAME]\n"
                 "                              create ~/.claude/csync and write config.json\n"
                 "  csync status [--json]       resolve every project's identity\n"
                 "  csync pull                  take what other devices pushed\n"
                 "  csync push                  publish what changed here\n"
                 "  csync sync [--dry-run]      pull then push\n"
                 "  csync help\n";
}

int cmd_init(const std::vector<std::string>& args) {
    std::string remote;
    std::string createName;
    bool assumeYes = false;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--remote" && i + 1 < args.size()) {
            remote = args[++i];
        } else if (args[i] == "--create-remote" && i + 1 < args.size()) {
            createName = args[++i];
        } else if (args[i] == "--yes" || args[i] == "-y") {
            assumeYes = true;
        } else {
            std::cerr << "csync init: unknown argument '" << args[i] << "'\n";
            return 2;
        }
    }

    if (!createName.empty()) {
        // Creating a GitHub repo is outward-facing and awkward to undo, so it
        // never happens as a side effect of setting up local config.
        std::cout << "About to create a PRIVATE GitHub repo named '" << createName
                  << "' on your authenticated account.\n";
        if (!assumeYes) {
            std::cout << "Proceed? [y/N] " << std::flush;
            std::string answer;
            std::getline(std::cin, answer);
            if (answer != "y" && answer != "Y" && answer != "yes") {
                std::cout << "Cancelled. Nothing was created.\n";
                return 1;
            }
        }

        std::string url, err;
        if (!create_remote_repo(createName, url, err)) {
            std::cerr << "csync: " << err << "\n";
            return 1;
        }
        std::cout << "created " << url << "\n";
        remote = url;
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

    return 0;
}

int report_sync(const SyncReport& r) {
    for (const auto& line : r.log) std::cout << "  " << line << "\n";

    for (const auto& e : r.errors) std::cerr << "csync: " << e << "\n";
    if (!r.errors.empty()) return 1;

    const auto& s = r.stats;
    if (!s.changed()) {
        std::cout << "up to date (" << r.projectsSynced << " projects)\n";
        return 0;
    }

    std::cout << "synced " << r.projectsSynced << " projects:";
    if (s.toRepo) std::cout << " " << s.toRepo << " up";
    if (s.toLocal) std::cout << " " << s.toLocal << " down";
    if (s.deletedRepo) std::cout << " " << s.deletedRepo << " removed from repo";
    if (s.deletedLocal) std::cout << " " << s.deletedLocal << " removed locally";
    if (s.conflicts) std::cout << " " << s.conflicts << " conflicts";
    std::cout << "\n";
    return 0;
}

int cmd_sync(const std::vector<std::string>& args, bool fetch, bool push) {
    SyncOptions opts;
    opts.fetch = fetch;
    opts.push = push;

    for (const auto& a : args) {
        if (a == "--dry-run" || a == "-n") {
            opts.dryRun = true;
        } else {
            std::cerr << "csync: unknown argument '" << a << "'\n";
            return 2;
        }
    }

    if (!config_exists()) {
        std::cerr << "csync: not set up yet; run 'csync init --remote <url>'\n";
        return 1;
    }

    return report_sync(run_sync(opts));
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
    if (cmd == "pull") return cmd_sync(rest, /*fetch=*/true, /*push=*/false);
    if (cmd == "push") return cmd_sync(rest, /*fetch=*/false, /*push=*/true);
    if (cmd == "sync") return cmd_sync(rest, /*fetch=*/true, /*push=*/true);

    std::cerr << "csync: unknown command '" << cmd << "'\n\n";
    print_usage();
    return 2;
}
