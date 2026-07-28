#include "repo.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <cstdint>
#include <sstream>

#include "git.h"
#include "manifest.h"
#include "paths.h"

namespace claude_sync {
namespace {

// True when manifest.json is the only thing git could not merge. Anything else
// unresolved means the memory files themselves collided, which is not this
// function's problem.
bool only_manifest_conflicted() {
    auto r = run_git(repo_path(), {"diff", "--name-only", "--diff-filter=U"});
    if (!r.ok() || r.out.empty()) return false;

    std::istringstream in(r.out);
    std::string line;
    bool sawManifest = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line != "manifest.json") return false;
        sawManifest = true;
    }
    return sawManifest;
}

std::string stage(const std::string& ref) {
    auto r = run_git(repo_path(), {"show", ref});
    return r.ok() ? r.out : std::string{};
}

}  // namespace

// Replaces a conflicted manifest.json with the union of both sides. Returns
// false if the conflict was anything else, leaving the merge untouched.
bool resolve_manifest_conflict() {
    if (!only_manifest_conflicted()) return false;

    Manifest ours = Manifest::parse(stage(":2:manifest.json"));
    Manifest theirs = Manifest::parse(stage(":3:manifest.json"));
    ours.merge_from(theirs);

    if (!ours.save(repo_path())) return false;

    auto add = run_git(repo_path(), {"add", "manifest.json"});
    return add.ok();
}

fs::path repo_path() { return sync_dir_path() / "repo"; }

bool repo_exists() {
    std::error_code ec;
    return fs::is_directory(repo_path() / ".git", ec);
}

std::string current_branch() {
    // symbolic-ref answers even on an unborn branch, which rev-parse does not.
    auto r = run_git(repo_path(), {"symbolic-ref", "--short", "HEAD"});
    if (r.ok() && !r.out.empty()) return r.out;
    return "main";
}

bool ensure_repo(const Config& c, std::string& err) {
    if (repo_exists()) {
        // Cheap and re-run every time, because the driver config is local to the
        // clone and silently stops working if the binary is moved or reinstalled.
        install_merge_driver();
        return true;
    }

    if (c.remoteUrl.empty()) {
        err = "no sync remote configured; run 'claude-sync init --remote <url>'";
        return false;
    }

    std::error_code ec;
    fs::create_directories(sync_dir_path(), ec);

    // A half-finished clone would be mistaken for a working repo next run.
    fs::remove_all(repo_path(), ec);

    auto r = run("git", {"clone", "--quiet", c.remoteUrl, repo_path().string()});
    if (!r.ok()) {
        fs::remove_all(repo_path(), ec);
        err = "clone failed: " + (r.err.empty() ? r.out : r.err);
        return false;
    }

    // A brand new remote has no branch at all. Name one now so the first push
    // has something to push.
    auto head = run_git(repo_path(), {"symbolic-ref", "--short", "HEAD"});
    if (!head.ok() || head.out.empty()) {
        run_git(repo_path(), {"checkout", "-q", "-b", "main"});
    }

    install_merge_driver();
    return true;
}

bool repo_pull(std::string& err) {
    auto fetch = run_git(repo_path(), {"fetch", "--quiet", "origin"});
    if (!fetch.ok()) {
        err = "fetch failed: " + (fetch.err.empty() ? fetch.out : fetch.err);
        return false;
    }

    std::string branch = current_branch();
    auto exists = run_git(repo_path(), {"rev-parse", "--verify", "--quiet", "origin/" + branch});
    if (!exists.ok() || exists.out.empty()) {
        return true;  // nothing upstream yet
    }

    auto local_head = run_git(repo_path(), {"rev-parse", "--verify", "--quiet", "HEAD"});
    if (!local_head.ok() || local_head.out.empty()) {
        // Unborn local branch with commits upstream: adopt them wholesale.
        auto reset = run_git(repo_path(), {"reset", "--hard", "origin/" + branch});
        if (!reset.ok()) {
            err = "could not adopt upstream history: " + reset.err;
            return false;
        }
        return true;
    }

    auto merge = run_git(repo_path(), {"merge", "--no-edit", "origin/" + branch});
    if (merge.ok()) return true;

    if (resolve_manifest_conflict()) {
        auto finish = run_git(repo_path(), {"commit", "--no-edit", "--quiet"});
        if (finish.ok()) return true;
    }

    // Step 3's merge driver is what resolves memory-file collisions. Until then
    // an unresolvable merge is left alone rather than force-resolved.
    run_git(repo_path(), {"merge", "--abort"});
    err = "merge failed: " + (merge.err.empty() ? merge.out : merge.err);
    return false;
}

bool repo_commit_and_push(const std::string& message, bool doPush, std::string& err,
                          PushResult* result) {
    PushResult local;
    PushResult& out = result ? *result : local;
    out = PushResult{};

    auto add = run_git(repo_path(), {"add", "-A"});
    if (!add.ok()) {
        err = "git add failed: " + add.err;
        return false;
    }

    auto staged = run_git(repo_path(), {"diff", "--cached", "--quiet"});
    bool hasChanges = staged.code != 0;

    if (hasChanges) {
        auto commit = run_git(repo_path(), {"commit", "--quiet", "-m", message});
        if (!commit.ok()) {
            err = "commit failed: " + (commit.err.empty() ? commit.out : commit.err);
            return false;
        }
        out.committed = true;
    }

    if (!doPush) return true;

    std::string branch = current_branch();
    auto head = run_git(repo_path(), {"rev-parse", "--verify", "--quiet", "HEAD"});
    if (!head.ok() || head.out.empty()) return true;  // nothing to push

    auto push = run_git(repo_path(), {"push", "--quiet", "-u", "origin", branch});
    if (push.ok()) return true;

    // Another device got there first. Rebase onto what it pushed and try again.
    auto fetch = run_git(repo_path(), {"fetch", "--quiet", "origin"});
    if (fetch.ok()) {
        auto rebase = run_git(repo_path(), {"rebase", "origin/" + branch});

        // The same manifest collision can surface here, where the race is most
        // likely to happen in the first place.
        if (!rebase.ok() && resolve_manifest_conflict()) {
            rebase = run_git(repo_path(), {"-c", "core.editor=true", "rebase", "--continue"});
        }

        if (rebase.ok()) {
            out.integratedRemote = true;
            auto retry = run_git(repo_path(), {"push", "--quiet", "-u", "origin", branch});
            if (retry.ok()) return true;
            err = "push failed after rebase: " + (retry.err.empty() ? retry.out : retry.err);
            return false;
        }
        run_git(repo_path(), {"rebase", "--abort"});
    }

    err = "push failed: " + (push.err.empty() ? push.out : push.err);
    return false;
}

fs::path self_path() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(&buf[0], &size) != 0) return {};
    buf.resize(std::char_traits<char>::length(buf.c_str()));
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(fs::path(buf), ec);
    return ec ? fs::path(buf) : resolved;
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path{} : p;
#endif
}

bool install_merge_driver() {
    fs::path repo = repo_path();
    std::error_code ec;
    if (!fs::is_directory(repo / ".git", ec)) return false;

    const std::string attributes =
        "# Memory files are merged by claude-sync, not by line-based diff.\n"
        "MEMORY.md merge=claude-memory\n"
        "*.md merge=claude-memory\n"
        "manifest.json merge=binary\n";

    fs::path attrPath = repo / ".gitattributes";
    if (read_file(attrPath) != attributes) {
        if (!write_atomic(attrPath, attributes)) return false;
    }

    fs::path self = self_path();
    if (self.empty()) return false;

    // %O base, %A ours (and the file the result must be written to), %B theirs,
    // %P the path in the worktree.
    std::string driver = "'" + self.string() + "' mergetool %O %A %B %P";

    auto set = run_git(repo, {"config", "merge.claude-memory.driver", driver});
    if (!set.ok()) return false;

    run_git(repo, {"config", "merge.claude-memory.name", "claude-sync memory merge"});
    return true;
}

bool gh_available() {
    auto r = run("sh", {"-c", "command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1"});
    return r.ok();
}

bool create_remote_repo(const std::string& name, std::string& urlOut, std::string& err) {
    if (!gh_available()) {
        err = "gh is not installed or not authenticated. Create the repo yourself with:\n"
              "  gh repo create " +
              name +
              " --private --clone=false\n"
              "then run: claude-sync init --remote <url>";
        return false;
    }

    auto create = run("gh", {"repo", "create", name, "--private", "--clone=false",
                             "--description", "Claude Code memory sync"});
    if (!create.ok()) {
        err = "gh repo create failed: " + (create.err.empty() ? create.out : create.err);
        return false;
    }

    auto view = run("gh", {"repo", "view", name, "--json", "sshUrl", "-q", ".sshUrl"});
    if (!view.ok() || view.out.empty()) {
        err = "repo was created but its URL could not be read back; set it with "
              "'claude-sync init --remote <url>'";
        return false;
    }

    urlOut = view.out;
    return true;
}

}  // namespace claude_sync
