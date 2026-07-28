#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace claude_sync {

enum class ProjectState {
    RemoteRepo,   // git repo with a remote -> remote/<host>/<owner>/<repo>
    LocalRepo,    // git repo, no remote    -> local/<root commit prefix>
    NotGit,       // not a repo, or a repo we refuse to adopt
    NoCommits,    // fresh git init, nothing to key on yet
    PathMissing,  // cwd recovered but the directory is gone
    NoCwd,        // no transcript carried a cwd
};

const char* state_name(ProjectState s);

struct Project {
    std::string escapedDir;
    fs::path cwd;
    fs::path repoRoot;
    ProjectState state = ProjectState::NoCwd;
    std::string id;
    std::string rootCommit;
    std::string remoteUrl;
    int memoryFileCount = -1;
    std::string note;

    bool synced() const {
        return state == ProjectState::RemoteRepo || state == ProjectState::LocalRepo;
    }
};

// Reduces every URL form git accepts to "host/owner/repo". Empty in, empty out.
std::string normalize_remote(const std::string& url);

// Case-insensitive compare, since GitHub treats owner and repo that way and two
// spellings of one repo must not become two entries.
bool same_id(const std::string& a, const std::string& b);

Project resolve(const fs::path& project_dir);

std::vector<Project> scan_all();

}  // namespace claude_sync
