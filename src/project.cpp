#include "project.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "git.h"
#include "paths.h"

namespace csync {
namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string collapse_slashes(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '/' && !out.empty() && out.back() == '/') continue;
        out += c;
    }
    return out;
}

}  // namespace

const char* state_name(ProjectState s) {
    switch (s) {
        case ProjectState::RemoteRepo: return "ok";
        case ProjectState::LocalRepo: return "ok-local";
        case ProjectState::NotGit: return "no-git";
        case ProjectState::NoCommits: return "no-commits";
        case ProjectState::PathMissing: return "missing";
        case ProjectState::NoCwd: return "no-cwd";
    }
    return "?";
}

std::string normalize_remote(const std::string& url) {
    std::string s = trim(url);
    if (s.empty()) return {};

    size_t scheme = s.find("://");
    if (scheme != std::string::npos) {
        s = s.substr(scheme + 3);
    } else {
        // scp-style "[user@]host:path" -- a colon before any slash.
        size_t colon = s.find(':');
        size_t slash = s.find('/');
        if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
            s = s.substr(0, colon) + "/" + s.substr(colon + 1);
        }
    }

    // Split authority from path before touching either, so a '@' or ':' further
    // down the path is left alone.
    size_t slash = s.find('/');
    std::string authority = (slash == std::string::npos) ? s : s.substr(0, slash);
    std::string path = (slash == std::string::npos) ? "" : s.substr(slash);

    // Userinfo: this is what makes https://user@github.com/o/r and
    // https://github.com/o/r resolve to the same identity.
    if (size_t at = authority.rfind('@'); at != std::string::npos) {
        authority = authority.substr(at + 1);
    }
    if (size_t colon = authority.find(':'); colon != std::string::npos) {
        authority = authority.substr(0, colon);
    }

    // Host is case-insensitive; owner and repo keep the case they were observed
    // with so the ID reads the way the repo is actually named.
    std::string out = lower(authority) + path;
    out = collapse_slashes(out);

    while (!out.empty() && out.back() == '/') out.pop_back();
    if (out.size() > 4 && out.compare(out.size() - 4, 4, ".git") == 0) {
        out.erase(out.size() - 4);
    }
    while (!out.empty() && out.back() == '/') out.pop_back();

    return out;
}

bool same_id(const std::string& a, const std::string& b) { return lower(a) == lower(b); }

Project resolve(const fs::path& project_dir) {
    Project p;
    p.escapedDir = project_dir.filename().string();
    p.memoryFileCount = count_memory_files(project_dir);

    std::string cwd = cwd_for_project_dir(project_dir);
    if (cwd.empty()) {
        p.state = ProjectState::NoCwd;
        p.note = "no transcript carried a cwd";
        return p;
    }
    p.cwd = fs::path(cwd);

    std::error_code ec;
    if (!fs::is_directory(p.cwd, ec)) {
        p.state = ProjectState::PathMissing;
        p.note = "directory no longer exists";
        return p;
    }

    if (!is_repo(p.cwd)) {
        p.state = ProjectState::NotGit;
        return p;
    }

    p.repoRoot = repo_root(p.cwd);
    if (p.repoRoot.empty()) {
        p.state = ProjectState::NotGit;
        return p;
    }

    // A stray `git init` in $HOME or / would otherwise pull every project under
    // it into one identity.
    fs::path canonical = fs::weakly_canonical(p.repoRoot, ec);
    if (ec) canonical = p.repoRoot;
    if (canonical == home_dir() || canonical == canonical.root_path()) {
        p.state = ProjectState::NotGit;
        p.note = "repo root is $HOME or /, refusing to adopt";
        p.repoRoot.clear();
        return p;
    }

    p.rootCommit = root_commit(p.repoRoot);
    if (p.rootCommit.empty()) {
        p.state = ProjectState::NoCommits;
        p.note = "no commits yet";
        return p;
    }

    p.remoteUrl = remote_url(p.repoRoot);
    if (!p.remoteUrl.empty()) {
        std::string normalized = normalize_remote(p.remoteUrl);
        if (!normalized.empty()) {
            p.state = ProjectState::RemoteRepo;
            p.id = "remote/" + normalized;
            return p;
        }
    }

    p.state = ProjectState::LocalRepo;
    p.id = "local/" + p.rootCommit.substr(0, 10);
    return p;
}

std::vector<Project> scan_all() {
    std::vector<Project> projects;
    for (const auto& dir : list_project_dirs()) {
        projects.push_back(resolve(dir));
    }

    // Two project directories can resolve to one repo -- a subdirectory opened
    // as its own project, or a folder renamed on disk. That is the design
    // working, but it is worth surfacing before step 2 starts merging them.
    std::map<std::string, int> counts;
    for (const auto& p : projects) {
        if (p.synced()) counts[lower(p.id)]++;
    }
    for (auto& p : projects) {
        if (p.synced() && counts[lower(p.id)] > 1) {
            p.note = "shares this id with another project directory";
        }
    }

    return projects;
}

}  // namespace csync
