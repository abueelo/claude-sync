#include "mirror.h"

#include <algorithm>
#include <set>

#include "paths.h"

namespace claude_sync {
namespace {

// Relative paths of every regular file under dir, recursively. Dotted files and
// directories are skipped so .git and .DS_Store never enter a mirror.
std::vector<std::string> list_rel_files(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;

    if (!fs::is_directory(dir, ec)) return out;

    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return out;

    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;

        std::string name = it->path().filename().string();
        if (!name.empty() && name[0] == '.') {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;

        out.push_back(fs::relative(it->path(), dir, ec).generic_string());
    }

    std::sort(out.begin(), out.end());
    return out;
}

std::string conflict_name(const std::string& rel, const std::string& machine) {
    fs::path p(rel);
    std::string ext = p.extension().string();
    std::string stem = p.stem().string();
    fs::path parent = p.parent_path();
    std::string name = stem + ".conflict-" + machine + ext;
    return parent.empty() ? name : (parent / name).generic_string();
}

void remove_local(const fs::path& file, const fs::path& stopAt) {
    std::error_code ec;
    fs::remove(file, ec);

    // Leaving behind a tree of empty directories after a delete makes the next
    // scan noisier for no reason.
    fs::path dir = file.parent_path();
    while (dir != stopAt && dir.has_parent_path()) {
        if (!fs::is_empty(dir, ec) || ec) break;
        if (!fs::remove(dir, ec) || ec) break;
        dir = dir.parent_path();
    }
}

}  // namespace

void SyncStats::add(const SyncStats& o) {
    toRepo += o.toRepo;
    toLocal += o.toLocal;
    deletedLocal += o.deletedLocal;
    deletedRepo += o.deletedRepo;
    conflicts += o.conflicts;
}

SyncStats sync_dir(const fs::path& localDir, const fs::path& repoDir, const Store& store,
                   FileSet& baseline, const std::string& machine,
                   std::vector<std::string>& log) {
    SyncStats stats;

    // Local is always plaintext on disk; the repo side may be sealed. Both are
    // read up front into logical-name maps so everything below is mode-agnostic.
    std::map<std::string, std::string> local;
    for (const auto& rel : list_rel_files(localDir)) local[rel] = read_file(localDir / rel);

    int unreadable = 0;
    std::map<std::string, std::string> repo = store_read_all(repoDir, store, &unreadable);
    if (unreadable > 0) {
        log.push_back(std::to_string(unreadable) +
                      " file(s) could not be decrypted and were left alone");
    }

    std::set<std::string> names;
    for (const auto& [n, _] : local) names.insert(n);
    for (const auto& [n, _] : repo) names.insert(n);
    for (const auto& [n, _] : baseline) names.insert(n);

    FileSet next;

    for (const auto& rel : names) {
        auto lit = local.find(rel);
        auto rit = repo.find(rel);
        bool hasL = lit != local.end();
        bool hasR = rit != repo.end();

        auto bit = baseline.find(rel);
        bool hasB = bit != baseline.end();
        std::string bh = hasB ? bit->second : std::string{};

        std::string lh = hasL ? hash_bytes(lit->second) : std::string{};
        std::string rh = hasR ? hash_bytes(rit->second) : std::string{};

        if (!hasL && !hasR) {
            continue;  // gone from both sides; drop it from the baseline
        }

        if (hasL && !hasR) {
            if (hasB) {
                // The other side deleted it and we still had the synced copy.
                if (lh == bh) {
                    remove_local(localDir / rel, localDir);
                    ++stats.deletedLocal;
                    log.push_back("deleted locally: " + rel);
                    continue;
                }
                // Deleted there, edited here. Keeping the edit is the only
                // choice that cannot lose work.
                store_write(repoDir, store, rel, lit->second);
                next[rel] = lh;
                ++stats.toRepo;
                log.push_back("kept locally-edited file the remote deleted: " + rel);
                continue;
            }
            store_write(repoDir, store, rel, lit->second);
            next[rel] = lh;
            ++stats.toRepo;
            continue;
        }

        if (!hasL && hasR) {
            if (hasB) {
                if (rh == bh) {
                    store_remove(repoDir, store, rel);
                    ++stats.deletedRepo;
                    log.push_back("deleted from repo: " + rel);
                    continue;
                }
                write_atomic(localDir / rel, rit->second);
                next[rel] = rh;
                ++stats.toLocal;
                log.push_back("kept remotely-edited file deleted here: " + rel);
                continue;
            }
            write_atomic(localDir / rel, rit->second);
            next[rel] = rh;
            ++stats.toLocal;
            continue;
        }

        // Present on both sides.
        if (lh == rh) {
            next[rel] = lh;
            continue;
        }

        bool localChanged = !hasB || lh != bh;
        bool repoChanged = !hasB || rh != bh;

        if (localChanged && !repoChanged) {
            store_write(repoDir, store, rel, lit->second);
            next[rel] = lh;
            ++stats.toRepo;
            continue;
        }
        if (!localChanged && repoChanged) {
            write_atomic(localDir / rel, rit->second);
            next[rel] = rh;
            ++stats.toLocal;
            continue;
        }

        // Both sides moved. Newer content wins and the loser is written beside
        // it, because the one thing worse than a conflict file is a silently
        // discarded memory.
        //
        // MEMORY.md is the file this actually happens to, and the git merge
        // driver unions it properly when the collision happens at the git
        // level. This branch is the mirror-level fallback, where only one side
        // has a real mtime to compare.
        bool localWins = true;
        {
            std::error_code ec;
            fs::path lp = localDir / rel;
            fs::path rp = store.file_for(repoDir, rel);
            if (fs::is_regular_file(lp, ec) && fs::is_regular_file(rp, ec)) {
                localWins = mtime_seconds(lp) >= mtime_seconds(rp);
            }
        }

        const std::string& winning = localWins ? lit->second : rit->second;
        const std::string& losing = localWins ? rit->second : lit->second;

        std::string cname = conflict_name(rel, machine);
        write_atomic(localDir / cname, losing);
        store_write(repoDir, store, cname, losing);
        next[cname] = hash_bytes(losing);

        write_atomic(localDir / rel, winning);
        store_write(repoDir, store, rel, winning);
        next[rel] = hash_bytes(winning);

        ++stats.conflicts;
        log.push_back("conflict on " + rel + ": kept the " + (localWins ? "local" : "remote") +
                      " copy, other side saved as " + cname);
    }

    baseline = std::move(next);
    return stats;
}

}  // namespace claude_sync
