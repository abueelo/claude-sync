#pragma once

#include <map>
#include <string>

#include "crypt.h"

namespace claude_sync {

// The repo side of a mirror, which is either plain files or sealed ones.
//
// Everything above this speaks in logical relative names ("MEMORY.md",
// "sub/dir/a.md"). Only this knows that an encrypted repo stores them under
// keyed hashes with the real name sealed inside the blob, so the three-way
// compare in mirror.cpp is identical in both modes.
struct Store {
    bool encrypted = false;
    Keys keys;
    std::string prefix;  // logical path this directory represents, for the envelope

    fs::path file_for(const fs::path& dir, const std::string& rel) const;
    std::string logical(const std::string& rel) const;

    // Where a logical directory lives inside the repo.
    //
    // Hashing the filenames is not enough on its own: the directory a memory
    // sits in is "projects/remote/github.com/<owner>/<repo>/memory", which
    // names a private repository in the clear. That is more identifying than
    // the memory text, so the directory is hashed too.
    fs::path dir_for(const fs::path& repo, const std::string& logicalDir) const;
};

// Logical name -> contents, for everything readable in dir. Files that fail to
// open under the current key are skipped rather than fatal: a repo can hold a
// blob written by a device whose key has since been rotated, and one unreadable
// file must not stop every other project from syncing.
std::map<std::string, std::string> store_read_all(const fs::path& dir, const Store& s,
                                                  int* unreadable = nullptr);

bool store_write(const fs::path& dir, const Store& s, const std::string& rel,
                 const std::string& contents);

bool store_remove(const fs::path& dir, const Store& s, const std::string& rel);

// Seals with the store's prefix applied, for callers holding whole documents
// rather than directory entries (the manifest).
std::string store_seal_blob(const Store& s, const std::string& logicalName,
                            const std::string& contents);
bool store_open_blob(const Store& s, const std::string& sealed, std::string& contentsOut);

}  // namespace claude_sync
