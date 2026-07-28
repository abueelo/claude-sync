#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "config.h"
#include "git.h"
#include "json.hpp"
#include "paths.h"
#include "merge.h"
#include "project.h"
#include "repo.h"
#include "sync.h"

using nlohmann::json;
using namespace claude_sync;

namespace {

void print_usage() {
    std::cout << "claude-sync - sync Claude Code memory across devices\n\n"
                 "usage:\n"
                 "  claude-sync init [--remote URL] [--create-remote NAME]\n"
                 "                              create ~/.claude/claude-sync and write config.json\n"
                 "  claude-sync status [--json]       resolve every project's identity\n"
                 "  claude-sync pull                  take what other devices pushed\n"
                 "  claude-sync push                  publish what changed here\n"
                 "  claude-sync sync [--dry-run]      pull then push\n"
                 "  claude-sync mergetool ...         git merge driver, called by git\n"
                 "  claude-sync help\n";
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
            std::cerr << "claude-sync init: unknown argument '" << args[i] << "'\n";
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
            std::cerr << "claude-sync: " << err << "\n";
            return 1;
        }
        std::cout << "created " << url << "\n";
        remote = url;
    }

    bool existed = config_exists();
    Config c = existed ? load_config() : default_config();
    if (!remote.empty()) c.remoteUrl = remote;

    if (!save_config(c)) {
        std::cerr << "claude-sync: could not write " << (sync_dir_path() / "config.json") << "\n";
        return 1;
    }

    std::cout << (existed ? "updated " : "created ") << (sync_dir_path() / "config.json") << "\n";
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
            std::cerr << "claude-sync status: unknown argument '" << a << "'\n";
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

    for (const auto& e : r.errors) std::cerr << "claude-sync: " << e << "\n";
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

// Git's merge driver contract: read the three versions, write the result over
// %A, exit 0 if resolved and non-zero to leave a conflict. Called by git during
// a merge, never by a person.
int cmd_mergetool(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "usage: claude-sync mergetool %O %A %B [%P]\n";
        return 2;
    }

    fs::path basePath = args[0];
    fs::path oursPath = args[1];
    fs::path theirsPath = args[2];
    std::string worktreePath = args.size() > 3 ? args[3] : std::string{};

    std::string base = read_file(basePath);
    std::string ours = read_file(oursPath);
    std::string theirs = read_file(theirsPath);

    if (ours == theirs) return 0;

    std::string name = fs::path(worktreePath).filename().string();
    if (name.empty()) name = oursPath.filename().string();

    if (name == "MEMORY.md") {
        std::string merged = merge_memory_index(base, ours, theirs);
        return write_atomic(oursPath, merged) ? 0 : 1;
    }

    // Any other memory file. The two temp files git hands over carry no useful
    // timestamps, so "newer wins" is not available here the way it is in the
    // mirror -- keep ours and park theirs beside it in the worktree instead.
    // Resolving rather than conflicting is what keeps a hook from ever wedging
    // the repo mid-merge.
    Config c = load_config();
    std::string machine = c.machineName.empty() ? "other-device" : c.machineName;

    fs::path rel(worktreePath.empty() ? name : worktreePath);
    std::string stem = rel.stem().string();
    std::string ext = rel.extension().string();
    fs::path parent = rel.parent_path();
    fs::path conflict = parent / (stem + ".conflict-" + machine + ext);

    // cwd during a merge driver is the top of the worktree.
    write_atomic(conflict, theirs);
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
            std::cerr << "claude-sync: unknown argument '" << a << "'\n";
            return 2;
        }
    }

    if (!config_exists()) {
        std::cerr << "claude-sync: not set up yet; run 'claude-sync init --remote <url>'\n";
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
    if (cmd == "mergetool") return cmd_mergetool(rest);

    std::cerr << "claude-sync: unknown command '" << cmd << "'\n\n";
    print_usage();
    return 2;
}
