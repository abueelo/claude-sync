#include "sync.h"

#include <algorithm>

#include "lock.h"
#include "manifest.h"
#include "paths.h"
#include "project.h"
#include "repo.h"

namespace claude_sync {
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

std::string commit_message(const SyncStats& s, const std::string& machine,
                           const std::vector<std::string>& names) {
    std::string what;
    if (s.toRepo > 0) {
        what = std::to_string(s.toRepo) + (s.toRepo == 1 ? " memory" : " memories");
    } else if (s.deletedRepo > 0) {
        what = std::to_string(s.deletedRepo) + " deleted";
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

// Reconciles the global CLAUDE.md, which is one file rather than a directory.
void sync_global_claude_md(const fs::path& repo, State& state, SyncReport& report) {
    fs::path lp = claude_dir() / "CLAUDE.md";
    fs::path rp = repo / "global" / "CLAUDE.md";

    FileSet baseline = state.baseline.count("global/CLAUDE.md") ? state.baseline["global/CLAUDE.md"]
                                                               : FileSet{};

    std::error_code ec;
    bool hasL = fs::is_regular_file(lp, ec);
    bool hasR = fs::is_regular_file(rp, ec);
    if (!hasL && !hasR) {
        state.baseline["global/CLAUDE.md"] = FileSet{};
        return;
    }

    std::string lh = hasL ? content_hash(lp) : std::string{};
    std::string rh = hasR ? content_hash(rp) : std::string{};

    if (lh == rh) {
        baseline["CLAUDE.md"] = lh;
        state.baseline["global/CLAUDE.md"] = baseline;
        return;
    }

    if (hasL && !hasR) {
        copy_file_over(lp, rp);
        ++report.stats.toRepo;
    } else if (!hasL && hasR) {
        copy_file_over(rp, lp);
        ++report.stats.toLocal;
    } else if (mtime_seconds(rp) > mtime_seconds(lp)) {
        copy_file_over(rp, lp);
        ++report.stats.toLocal;
        report.log.push_back("global: took the newer CLAUDE.md from the repo");
    } else {
        copy_file_over(lp, rp);
        ++report.stats.toRepo;
    }

    baseline["CLAUDE.md"] = content_hash(lp);
    state.baseline["global/CLAUDE.md"] = baseline;
}

// One reconciliation pass over every project plus the global material. Runs
// again after a rebase, since integrating another device's commits leaves the
// repo holding files the local memory dir has not seen.
SyncStats reconcile(const Config& c, const fs::path& repo, State& state, Manifest& manifest,
                    const std::vector<Project>& projects, SyncReport& report,
                    std::vector<std::string>& touched) {
    SyncStats total;

    for (const auto& p : projects) {
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

        Entry* known = manifest.find_by_id(p.id);
        std::string entryId = known ? known->id : p.id;

        fs::path localMem = projects_dir() / p.escapedDir / "memory";
        fs::path repoMem = repo / "projects" / entryId / "memory";

        FileSet baseline = state.baseline.count(entryId) ? state.baseline[entryId] : FileSet{};

        std::vector<std::string> plog;
        SyncStats s = sync_dir(localMem, repoMem, baseline, c.machineName, plog);

        state.baseline[entryId] = std::move(baseline);
        total.add(s);

        manifest.upsert(p, c.machineName, s.changed());

        if (s.changed()) {
            std::string name = p.cwd.filename().string();
            if (std::find(touched.begin(), touched.end(), name) == touched.end()) {
                touched.push_back(name);
            }
            for (const auto& line : plog) report.log.push_back(name + ": " + line);
        }
    }

    if (c.scope.globalClaudeMd) sync_global_claude_md(repo, state, report);

    for (const auto& g : global_pairs(c, repo)) {
        if (!g.enabled) continue;
        std::error_code ec;
        if (!fs::is_directory(g.local, ec) && !fs::is_directory(g.repo, ec)) continue;

        std::string key = "global/" + g.local.filename().string();
        FileSet baseline = state.baseline.count(key) ? state.baseline[key] : FileSet{};

        std::vector<std::string> glog;
        SyncStats s = sync_dir(g.local, g.repo, baseline, c.machineName, glog);

        state.baseline[key] = std::move(baseline);
        total.add(s);
        for (const auto& line : glog) report.log.push_back(key + ": " + line);
    }

    manifest.save(repo);
    return total;
}

}  // namespace

SyncReport run_sync(const SyncOptions& opts) {
    SyncReport report;

    Config c = load_config();

    Lock lock;
    if (!lock.acquire(sync_dir_path() / "claude-sync.lock")) {
        report.errors.push_back("another claude-sync run holds the lock; skipping");
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

    report.stats.add(reconcile(c, repo, state, manifest, projects, report, touched));
    for (const auto& p : projects) {
        if (p.synced()) ++report.projectsSynced;
    }

    state.projects = projects;
    state.lastSync = now_iso8601();

    if (opts.dryRun) {
        report.log.push_back("dry run: nothing committed, pushed, or recorded");
        return report;
    }

    PushResult push;
    if (!repo_commit_and_push(commit_message(report.stats, c.machineName, touched), opts.push, err,
                              &push)) {
        report.errors.push_back(err);
        // The working tree is already reconciled, so the baseline still has to
        // be recorded or the next run would redo everything as if new.
        save_state(state);
        return report;
    }
    report.committed = push.committed;

    if (push.integratedRemote) {
        // The rebase brought in another device's commits, and possibly a
        // sidecar the merge driver wrote. Neither has reached ~/.claude yet.
        report.log.push_back("integrated another device's changes mid-push, reconciling again");

        manifest = Manifest::load(repo);
        SyncStats second = reconcile(c, repo, state, manifest, projects, report, touched);
        report.stats.add(second);

        if (second.changed()) {
            PushResult again;
            if (!repo_commit_and_push(commit_message(second, c.machineName, touched), opts.push,
                                      err, &again)) {
                report.errors.push_back(err);
                save_state(state);
                return report;
            }
            report.committed = report.committed || again.committed;
        }
    }

    state.lastSync = now_iso8601();
    save_state(state);
    return report;
}

}  // namespace claude_sync
