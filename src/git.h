#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace csync {

struct GitResult {
    int code = -1;
    std::string out;
    std::string err;

    bool ok() const { return code == 0; }
};

// Runs a command, capturing stdout and stderr separately.
GitResult run(const std::string& program, const std::vector<std::string>& args);

// Runs git with -C <cwd> prepended.
GitResult run_git(const fs::path& cwd, const std::vector<std::string>& args);

bool is_repo(const fs::path& cwd);
fs::path repo_root(const fs::path& cwd);

// origin if it exists, otherwise the first remote listed. Empty if none.
std::string remote_url(const fs::path& root);

// Oldest root commit, full 40-char SHA. Empty if the repo has no commits.
std::string root_commit(const fs::path& root);

}  // namespace csync
