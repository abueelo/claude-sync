#include "git.h"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace csync {
namespace {

std::string shell_quote(const std::string& s) {
    std::string q = "'";
    for (char c : s) {
        if (c == '\'') {
            q += "'\\''";
        } else {
            q += c;
        }
    }
    q += "'";
    return q;
}

std::string read_file(const fs::path& p) {
    std::FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) return {};
    std::string out;
    std::array<char, 4096> buf{};
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
        out.append(buf.data(), n);
    }
    std::fclose(f);
    return out;
}

void trim_trailing_newlines(std::string& s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
}

}  // namespace

GitResult run(const std::string& program, const std::vector<std::string>& args) {
    GitResult r;

    // stderr goes to a temp file so it never contaminates the parsed stdout.
    std::error_code ec;
    fs::path errfile = fs::temp_directory_path(ec) / ("csync-err-" + std::to_string(::getpid()) +
                                                      "-" + std::to_string(rand()) + ".txt");

    std::ostringstream cmd;
    cmd << shell_quote(program);
    for (const auto& a : args) cmd << ' ' << shell_quote(a);
    cmd << " 2>" << shell_quote(errfile.string());

    std::FILE* pipe = ::popen(cmd.str().c_str(), "r");
    if (!pipe) {
        fs::remove(errfile, ec);
        r.err = "failed to spawn: " + program;
        return r;
    }

    std::array<char, 4096> buf{};
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        r.out.append(buf.data(), n);
    }

    int status = ::pclose(pipe);
    r.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    r.err = read_file(errfile);
    fs::remove(errfile, ec);

    trim_trailing_newlines(r.out);
    trim_trailing_newlines(r.err);
    return r;
}

GitResult run_git(const fs::path& cwd, const std::vector<std::string>& args) {
    std::vector<std::string> full{"-C", cwd.string()};
    full.insert(full.end(), args.begin(), args.end());
    return run("git", full);
}

bool is_repo(const fs::path& cwd) {
    auto r = run_git(cwd, {"rev-parse", "--is-inside-work-tree"});
    return r.ok() && r.out == "true";
}

fs::path repo_root(const fs::path& cwd) {
    auto r = run_git(cwd, {"rev-parse", "--show-toplevel"});
    if (!r.ok() || r.out.empty()) return {};
    return fs::path(r.out);
}

std::string remote_url(const fs::path& root) {
    auto origin = run_git(root, {"remote", "get-url", "origin"});
    if (origin.ok() && !origin.out.empty()) return origin.out;

    auto names = run_git(root, {"remote"});
    if (!names.ok() || names.out.empty()) return {};

    std::istringstream in(names.out);
    std::string first;
    if (!std::getline(in, first) || first.empty()) return {};

    auto url = run_git(root, {"remote", "get-url", first});
    return url.ok() ? url.out : std::string{};
}

std::string root_commit(const fs::path& root) {
    auto r = run_git(root, {"rev-list", "--max-parents=0", "HEAD"});
    if (!r.ok() || r.out.empty()) return {};

    // A repo can have several root commits after a graft or subtree merge.
    // rev-list prints newest first, so the last line is the oldest and is the
    // one that stays stable as history grows.
    std::istringstream in(r.out);
    std::string line, last;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) last = line;
    }
    return last;
}

}  // namespace csync
