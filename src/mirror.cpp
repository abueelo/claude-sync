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

void remove_and_prune(const fs::path& file, const fs::path& stopAt) {
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

SyncStats sync_dir(const fs::path& localDir, const fs::path& repoDir, FileSet& baseline,
                   const std::string& machine, std::vector<std::string>& log) {
    SyncStats stats;

    std::set<std::string> names;
    for (const auto& n : list_rel_files(localDir)) names.insert(n);
    for (const auto& n : list_rel_files(repoDir)) names.insert(n);
    for (const auto& [n, _] : baseline) names.insert(n);

    FileSet next;

    for (const auto& rel : names) {
        fs::path lp = localDir / rel;
        fs::path rp = repoDir / rel;

        std::error_code ec;
        bool hasL = fs::is_regular_file(lp, ec);
        bool hasR = fs::is_regular_file(rp, ec);

        auto bit = baseline.find(rel);
        bool hasB = bit != baseline.end();
        std::string bh = hasB ? bit->second : std::string{};

        std::string lh = hasL ? content_hash(lp) : std::string{};
        std::string rh = hasR ? content_hash(rp) : std::string{};

        if (!hasL && !hasR) {
            continue;  // gone from both sides; drop it from the baseline
        }

        if (hasL && !hasR) {
            if (hasB) {
                // The other side deleted it and we still had the synced copy.
                if (lh == bh) {
                    remove_and_prune(lp, localDir);
                    ++stats.deletedLocal;
                    log.push_back("deleted locally: " + rel);
                    continue;
                }
                // Deleted there, edited here. Keeping the edit is the only
                // choice that cannot lose work.
                copy_file_over(lp, rp);
                next[rel] = lh;
                ++stats.toRepo;
                log.push_back("kept locally-edited file the remote deleted: " + rel);
                continue;
            }
            copy_file_over(lp, rp);
            next[rel] = lh;
            ++stats.toRepo;
            continue;
        }

        if (!hasL && hasR) {
            if (hasB) {
                if (rh == bh) {
                    remove_and_prune(rp, repoDir);
                    ++stats.deletedRepo;
                    log.push_back("deleted from repo: " + rel);
                    continue;
                }
                copy_file_over(rp, lp);
                next[rel] = rh;
                ++stats.toLocal;
                log.push_back("kept remotely-edited file deleted here: " + rel);
                continue;
            }
            copy_file_over(rp, lp);
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
            copy_file_over(lp, rp);
            next[rel] = lh;
            ++stats.toRepo;
            continue;
        }
        if (!localChanged && repoChanged) {
            copy_file_over(rp, lp);
            next[rel] = rh;
            ++stats.toLocal;
            continue;
        }

        // Both sides moved. Newer content wins and the loser is written beside
        // it, because the one thing worse than a conflict file is a silently
        // discarded memory.
        //
        // MEMORY.md is the file this actually happens to, and a union merge is
        // the right answer for it rather than picking a winner at all. That is
        // the merge driver, which lands in step 3 and takes over this case.
        bool localWins = mtime_seconds(lp) >= mtime_seconds(rp);
        const fs::path& winner = localWins ? lp : rp;
        const fs::path& loser = localWins ? rp : lp;

        std::string winning = read_file(winner);
        std::string losing = read_file(loser);

        std::string cname = conflict_name(rel, machine);
        write_atomic(localDir / cname, losing);
        write_atomic(repoDir / cname, losing);
        next[cname] = hash_bytes(losing);

        write_atomic(lp, winning);
        write_atomic(rp, winning);
        next[rel] = hash_bytes(winning);

        ++stats.conflicts;
        log.push_back("conflict on " + rel + ": kept the " + (localWins ? "local" : "remote") +
                      " copy, other side saved as " + cname);
    }

    baseline = std::move(next);
    return stats;
}

}  // namespace claude_sync
