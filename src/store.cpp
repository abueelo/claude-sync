#include "store.h"

#include <algorithm>
#include <vector>

#include "paths.h"

namespace claude_sync {
namespace {

std::vector<std::string> list_rel(const fs::path& dir) {
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

void prune_empty(const fs::path& file, const fs::path& stopAt) {
    std::error_code ec;
    fs::path dir = file.parent_path();
    while (dir != stopAt && dir.has_parent_path()) {
        if (!fs::is_empty(dir, ec) || ec) break;
        if (!fs::remove(dir, ec) || ec) break;
        dir = dir.parent_path();
    }
}

}  // namespace

std::string Store::logical(const std::string& rel) const {
    return prefix.empty() ? rel : prefix + "/" + rel;
}

fs::path Store::dir_for(const fs::path& repo, const std::string& logicalDir) const {
    if (!encrypted) return repo / logicalDir;
    // Flat under one directory, so the shape of the tree reveals nothing about
    // how many projects there are or how they nest.
    return repo / "d" / hash_path(keys, logicalDir);
}

fs::path Store::file_for(const fs::path& dir, const std::string& rel) const {
    if (!encrypted) return dir / rel;
    // Flat, because the directory shape would otherwise leak how memories are
    // organised even with the names hidden.
    return dir / (hash_path(keys, logical(rel)) + ".bin");
}

std::map<std::string, std::string> store_read_all(const fs::path& dir, const Store& s,
                                                  int* unreadable) {
    std::map<std::string, std::string> out;
    if (unreadable) *unreadable = 0;

    for (const auto& rel : list_rel(dir)) {
        if (!s.encrypted) {
            out[rel] = read_file(dir / rel);
            continue;
        }

        if (rel.size() < 4 || rel.compare(rel.size() - 4, 4, ".bin") != 0) continue;

        std::string logical, contents;
        if (!open_sealed(s.keys, read_file(dir / rel), logical, contents)) {
            if (unreadable) ++*unreadable;
            continue;
        }

        // Strip the prefix back off to get the name the caller thinks in.
        std::string name = logical;
        if (!s.prefix.empty()) {
            std::string p = s.prefix + "/";
            if (name.compare(0, p.size(), p) != 0) continue;  // belongs to another mirror
            name = name.substr(p.size());
        }
        out[name] = std::move(contents);
    }
    return out;
}

bool store_write(const fs::path& dir, const Store& s, const std::string& rel,
                 const std::string& contents) {
    fs::path target = s.file_for(dir, rel);
    if (!s.encrypted) return write_atomic(target, contents);
    return write_atomic(target, seal(s.keys, s.logical(rel), contents));
}

bool store_remove(const fs::path& dir, const Store& s, const std::string& rel) {
    fs::path target = s.file_for(dir, rel);
    std::error_code ec;
    bool removed = fs::remove(target, ec);
    prune_empty(target, dir);
    return removed;
}

std::string store_seal_blob(const Store& s, const std::string& logicalName,
                            const std::string& contents) {
    if (!s.encrypted) return contents;
    return seal(s.keys, logicalName, contents);
}

bool store_open_blob(const Store& s, const std::string& sealed, std::string& contentsOut) {
    if (!s.encrypted) {
        contentsOut = sealed;
        return true;
    }
    std::string ignoredPath;
    return open_sealed(s.keys, sealed, ignoredPath, contentsOut);
}

}  // namespace claude_sync
