#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "mirror.h"

namespace claude_sync {

struct SyncReport {
    SyncStats stats;
    std::vector<std::string> log;
    std::vector<std::string> errors;
    int projectsSynced = 0;
    bool committed = false;
};

struct SyncOptions {
    bool fetch = true;   // take what other devices pushed first
    bool push = true;    // publish what this device changed
    bool dryRun = false;
};

// The whole pipeline: fetch, reconcile every project plus the global files,
// update the manifest, commit and push.
SyncReport run_sync(const SyncOptions& opts);

}  // namespace claude_sync
