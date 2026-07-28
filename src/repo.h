#pragma once

#include <string>

#include "config.h"
#include "store.h"

namespace claude_sync {

fs::path repo_path();

bool repo_exists();

// Clones the sync repo if it is not there yet. A freshly created remote has no
// commits, which git clones happily but leaves on an unborn branch -- that case
// is normal here, not an error.
bool ensure_repo(const Config& c, std::string& err);

// Fetches and fast-forwards. A repo with no upstream commits yet is a success
// with nothing to do.
bool repo_pull(const Store& store, std::string& err);

struct PushResult {
    bool committed = false;

    // True when the push raced another device and we rebased onto its work. The
    // repo working tree now holds commits the local memory dir has never seen,
    // so the caller has to reconcile a second time -- otherwise those files sit
    // in the clone and never reach ~/.claude, and any sidecar the merge driver
    // wrote stays uncommitted.
    bool integratedRemote = false;
};

// Stages everything, commits if anything is staged, and pushes. Retries once
// through a rebase if the push races another device.
bool repo_commit_and_push(const std::string& message, bool doPush, const Store& store,
                          std::string& err, PushResult* result = nullptr);

std::string current_branch();

// Resolves a conflicted manifest.json by unioning both sides and staging it.
// False if anything other than the manifest is unresolved.
bool resolve_manifest_conflict(const Store& store);

// Writes .gitattributes and points merge.claude-memory.driver at this binary.
//
// The attributes file is committed and travels with the repo, but the driver
// definition lives in local git config and does not, so every machine has to
// register it -- and re-register it if the binary moves.
bool install_merge_driver();

// Absolute path of the running executable, so the driver config can name it.
fs::path self_path();

// Creates the private sync repo through the gh CLI and returns its ssh URL.
bool create_remote_repo(const std::string& name, std::string& urlOut, std::string& err);

bool gh_available();

}  // namespace claude_sync
