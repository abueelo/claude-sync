#pragma once

#include <string>
#include <vector>

#include "config.h"

namespace csync {

struct SyncStats {
    int toRepo = 0;
    int toLocal = 0;
    int deletedLocal = 0;
    int deletedRepo = 0;
    int conflicts = 0;

    bool changed() const {
        return toRepo || toLocal || deletedLocal || deletedRepo || conflicts;
    }
    void add(const SyncStats& o);
};

// Reconciles one directory pair against the baseline of what the last sync saw.
//
// The baseline is what makes a delete distinguishable from an addition: a file
// on one side only is a new file if the baseline never had it, and a deletion if
// it did. Without that third input the two are the same observation, and a sync
// tool that guesses will eventually resurrect something the user deleted.
//
// `baseline` is updated in place to describe the reconciled state.
SyncStats sync_dir(const fs::path& localDir, const fs::path& repoDir, FileSet& baseline,
                   const std::string& machine, std::vector<std::string>& log);

}  // namespace csync
