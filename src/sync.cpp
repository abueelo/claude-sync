#include "sync.h"

#include <algorithm>

#include "lock.h"
#include "manifest.h"
#include "paths.h"
#include "project.h"
#include "repo.h"

namespace csync {
namespace {

// Global material is shared by every project, so it lives at one path in the
// sync repo rather than under any project id.
struct GlobalPair {
    fs::path local;
    fs::path repo;
    bool enabled;
};

std::vector<GlobalPair> global_pairs(const Config& c, const fs::path& repo) {
    return {
        {claude_dir() / "skills", repo / "global" / "skills", c.scope.skills},
        {claude_dir() / "agents", repo / "global" / "agents", c.scope.agents},
        {claude_dir() / "commands", repo / "global" / "commands", c.scope.commands},
    };
}

std::string commit_message(const SyncReport& r, const std::string& machine,
                           const std::vector<std::string>& names) {
    std::string what;
    if (r.stats.toRepo > 0) {
        what = std::to_string(r.stats.toRepo) +
               (r.stats.toRepo == 1 ? " memory" : " memories");
    } else if (r.stats.deletedRepo > 0) {
        what = std::to_string(r.stats.deletedRepo) + " deleted";
    } else {
        what = "sync";
    }

    std::string where;
    if (names.size() == 1) {
        where = names[0];
    } else if (names.size() > 1) {
        where = std::to_string(names.size()) + " projects";
    } else {
        where = "global";
    }

    return where + ": " + what + " from " + machine + ", " + now_iso8601().substr(0, 10);
}

}  // namespace

SyncReport run_sync(const SyncOptions& opts) {
    SyncReport report;

    Config c = load_config();

    Lock lock;
    if (!lock.acquire(csync_dir() / "csync.lock")) {
        report.errors.push_back("another csync run holds the lock; skipping");
        return report;
    }

    std::string err;
    if (!ensure_repo(c, err)) {
        report.errors.push_back(err);
        return report;
    }

    fs::path repo = repo_path();

    if (opts.fetch && !repo_pull(err)) {
        // Reconciling against a stale checkout would push work that silently
        // reverts another device, so a failed fetch stops the run here.
        report.errors.push_back(err);
        return report;
    }

    State state = load_state();
    Manifest manifest = Manifest::load(repo);

    std::vector<Project> projects = scan_all();
    std::vector<std::string> touched;

    for (auto& p : projects) {
        if (!p.synced()) continue;
        if (!c.scope.memory) break;

        Manifest::Match how = Manifest::Match::None;
        manifest.resolve(p, &how);
        if (how == Manifest::Match::RootCommit || how == Manifest::Match::RemoteAlias) {
            // The project is known but its id has moved -- a rename, a transfer,
            // or a local repo that just gained a remote. Relinking (the git mv
            // and the canonicalSince check that stops a stale device renaming
            // backwards) is step 4. Until then this is reported, not acted on.
            report.log.push_back("id changed for " + p.cwd.filename().string() +
                                 "; relink lands in step 4, syncing under " + p.id);
        }

        // Reconcile first, then record: whether anything moved is exactly what
        // decides if the manifest timestamps should advance.
        Entry* known = manifest.find_by_id(p.id);
        std::string entryId = known ? known->id : p.id;

        fs::path localMem = projects_dir() / p.escapedDir / "memory";
        fs::path repoMem = repo / "projects" / entryId / "memory";

        FileSet baseline = state.baseline.count(entryId) ? state.baseline[entryId] : FileSet{};

        std::vector<std::string> plog;
        SyncStats s = sync_dir(localMem, repoMem, baseline, c.machineName, plog);

        state.baseline[entryId] = std::move(baseline);
        report.stats.add(s);
        ++report.projectsSynced;

        manifest.upsert(p, c.machineName, s.changed());

        if (s.changed()) {
            touched.push_back(p.cwd.filename().string());
            for (const auto& line : plog) {
                report.log.push_back(p.cwd.filename().string() + ": " + line);
            }
        }
    }

    // Global CLAUDE.md is a single file, so it gets a directory pair of its own
    // with a one-file view rather than a special case in sync_dir.
    if (c.scope.globalClaudeMd) {
        FileSet baseline =
            state.baseline.count("global/CLAUDE.md") ? state.baseline["global/CLAUDE.md"] : FileSet{};
        std::vector<std::string> glog;
        // Both sides are the containing directory, filtered to the one filename
        // by giving sync_dir a directory that holds only it.
        fs::path localDir = claude_dir();
        fs::path repoDir = repo / "global";
        FileSet single;
        if (baseline.count("CLAUDE.md")) single["CLAUDE.md"] = baseline["CLAUDE.md"];

        std::error_code ec;
        bool anySide = fs::is_regular_file(localDir / "CLAUDE.md", ec) ||
                       fs::is_regular_file(repoDir / "CLAUDE.md", ec) || !single.empty();
        if (anySide) {
            fs::path lp = localDir / "CLAUDE.md";
            fs::path rp = repoDir / "CLAUDE.md";
            std::string lh = fs::is_regular_file(lp, ec) ? content_hash(lp) : "";
            std::string rh = fs::is_regular_file(rp, ec) ? content_hash(rp) : "";

            if (!lh.empty() && lh != rh) {
                bool repoNewer = !rh.empty() && mtime_seconds(rp) > mtime_seconds(lp);
                if (repoNewer) {
                    copy_file_over(rp, lp);
                    ++report.stats.toLocal;
                } else {
                    copy_file_over(lp, rp);
                    ++report.stats.toRepo;
                }
            } else if (lh.empty() && !rh.empty()) {
                copy_file_over(rp, lp);
                ++report.stats.toLocal;
            }
            single["CLAUDE.md"] = content_hash(lp);
        }
        state.baseline["global/CLAUDE.md"] = single;
        for (const auto& line : glog) report.log.push_back("global: " + line);
    }

    for (const auto& g : global_pairs(c, repo)) {
        if (!g.enabled) continue;
        std::error_code ec;
        if (!fs::is_directory(g.local, ec) && !fs::is_directory(g.repo, ec)) continue;

        std::string key = "global/" + g.local.filename().string();
        FileSet baseline = state.baseline.count(key) ? state.baseline[key] : FileSet{};

        std::vector<std::string> glog;
        SyncStats s = sync_dir(g.local, g.repo, baseline, c.machineName, glog);

        state.baseline[key] = std::move(baseline);
        report.stats.add(s);
        for (const auto& line : glog) report.log.push_back(key + ": " + line);
    }

    manifest.save(repo);

    state.projects = projects;
    state.lastSync = now_iso8601();

    if (opts.dryRun) {
        report.log.push_back("dry run: nothing committed, pushed, or recorded");
        return report;
    }

    bool committed = false;
    if (!repo_commit_and_push(commit_message(report, c.machineName, touched), opts.push, err,
                              &committed)) {
        report.errors.push_back(err);
        // The working tree is already reconciled, so the baseline still has to
        // be recorded or the next run would redo everything as if new.
        save_state(state);
        return report;
    }

    report.committed = committed;
    save_state(state);
    return report;
}

}  // namespace csync
