#pragma once

#include <string>

#include "config.h"

namespace csync {

fs::path repo_path();

bool repo_exists();

// Clones the sync repo if it is not there yet. A freshly created remote has no
// commits, which git clones happily but leaves on an unborn branch -- that case
// is normal here, not an error.
bool ensure_repo(const Config& c, std::string& err);

// Fetches and fast-forwards. A repo with no upstream commits yet is a success
// with nothing to do.
bool repo_pull(std::string& err);

// Stages everything, commits if anything is staged, and pushes. Retries once
// through a rebase if the push races another device.
bool repo_commit_and_push(const std::string& message, bool doPush, std::string& err,
                          bool* committed = nullptr);

std::string current_branch();

// Resolves a conflicted manifest.json by unioning both sides and staging it.
// False if anything other than the manifest is unresolved.
bool resolve_manifest_conflict();

// Creates the private sync repo through the gh CLI and returns its ssh URL.
bool create_remote_repo(const std::string& name, std::string& urlOut, std::string& err);

bool gh_available();

}  // namespace csync
