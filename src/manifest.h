#pragma once

#include <map>
#include <string>
#include <vector>

#include "project.h"
#include "store.h"

namespace claude_sync {

// One project as the sync repo knows it. The id is the directory the memory
// lives under; everything else is what lets a different machine, or this one
// after a rename, recognise the same project again.
struct Entry {
    std::string id;
    std::string rootCommit;
    std::vector<std::string> remotes;  // every normalized remote ever seen
    std::vector<std::string> names;    // every folder name ever seen, newest first
    std::map<std::string, std::string> machines;  // machine -> last sync time
    std::string canonicalSince;                   // when this id became current
    std::string updated;
};

class Manifest {
public:
    static Manifest load(const fs::path& repo, const Store& store);
    static Manifest parse(const std::string& text);
    bool save(const fs::path& repo, const Store& store) const;

    // Where the manifest lives in the repo. Encrypted repos keep it sealed
    // under a different name, since manifest.json holds remote URLs, folder
    // names and machine names -- the most identifying material in the repo.
    static std::string filename(const Store& store);

    // Unions another manifest into this one, entry by entry.
    //
    // Two devices syncing near-simultaneously both rewrite manifest.json, and a
    // line-based merge of that is meaningless -- it is a set of records, not
    // prose. Merging it structurally means a manifest collision can always be
    // resolved rather than stalling the sync.
    void merge_from(const Manifest& other);

    // Lookup order matters. The id is derived from the remote and remotes
    // change, so a miss on the id is not proof this project is new: the root
    // commit survives a rename or transfer, and a stale device still reporting
    // the old URL resolves through the alias list.
    Entry* find_by_id(const std::string& id);
    Entry* find_by_root_commit(const std::string& rootCommit);
    Entry* find_by_remote(const std::string& normalizedRemote);

    // Resolves a scanned project to its entry, or nullptr if genuinely new.
    // Reports which key matched so the caller can tell a plain hit from a
    // rename that step 4 will act on.
    enum class Match { None, Id, RootCommit, RemoteAlias };
    Entry* resolve(const Project& p, Match* how);

    // `touched` records whether this run actually moved any of the project's
    // files. When nothing moved, the timestamps are left alone so the manifest
    // stays byte-identical and a no-op sync produces no commit -- which matters
    // because the Stop hook runs on every assistant turn.
    Entry& upsert(const Project& p, const std::string& machine, bool touched);

    std::vector<Entry>& entries() { return entries_; }
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

}  // namespace claude_sync
