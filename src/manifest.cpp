#include "manifest.h"

#include <algorithm>
#include <fstream>

#include "json.hpp"
#include "paths.h"

using nlohmann::json;

namespace claude_sync {
namespace {

void push_unique(std::vector<std::string>& v, const std::string& s) {
    if (s.empty()) return;
    for (const auto& e : v) {
        if (same_id(e, s)) return;
    }
    v.push_back(s);
}

// Newest first, so names[0] is the folder this project was last seen under.
void push_front_unique(std::vector<std::string>& v, const std::string& s) {
    if (s.empty()) return;
    v.erase(std::remove(v.begin(), v.end(), s), v.end());
    v.insert(v.begin(), s);
}

std::string strip_remote_prefix(const std::string& id) {
    const std::string prefix = "remote/";
    if (id.compare(0, prefix.size(), prefix) == 0) return id.substr(prefix.size());
    return {};
}

}  // namespace

Manifest Manifest::load(const fs::path& repo) {
    return parse(read_file(repo / "manifest.json"));
}

Manifest Manifest::parse(const std::string& text) {
    Manifest m;
    if (text.empty()) return m;

    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return m;
    if (!j.contains("entries") || !j["entries"].is_object()) return m;

    for (const auto& [id, e] : j["entries"].items()) {
        if (!e.is_object()) continue;
        Entry entry;
        entry.id = id;
        entry.rootCommit = e.value("rootCommit", std::string{});
        entry.canonicalSince = e.value("canonicalSince", std::string{});
        entry.updated = e.value("updated", std::string{});

        if (e.contains("remotes") && e["remotes"].is_array()) {
            for (const auto& r : e["remotes"]) {
                if (r.is_string()) entry.remotes.push_back(r.get<std::string>());
            }
        }
        if (e.contains("names") && e["names"].is_array()) {
            for (const auto& n : e["names"]) {
                if (n.is_string()) entry.names.push_back(n.get<std::string>());
            }
        }
        if (e.contains("machines") && e["machines"].is_object()) {
            for (const auto& [k, v] : e["machines"].items()) {
                if (v.is_string()) entry.machines[k] = v.get<std::string>();
            }
        }
        m.entries_.push_back(std::move(entry));
    }

    std::sort(m.entries_.begin(), m.entries_.end(),
              [](const Entry& a, const Entry& b) { return a.id < b.id; });
    return m;
}

bool Manifest::save(const fs::path& repo) const {
    json entries = json::object();
    for (const auto& e : entries_) {
        json j;
        j["rootCommit"] = e.rootCommit;
        j["remotes"] = e.remotes;
        j["names"] = e.names;
        j["machines"] = e.machines;
        j["canonicalSince"] = e.canonicalSince;
        j["updated"] = e.updated;
        entries[e.id] = j;
    }

    json j;
    j["version"] = 1;
    j["entries"] = entries;

    return write_atomic(repo / "manifest.json", j.dump(2) + "\n");
}

Entry* Manifest::find_by_id(const std::string& id) {
    if (id.empty()) return nullptr;
    for (auto& e : entries_) {
        if (same_id(e.id, id)) return &e;
    }
    return nullptr;
}

Entry* Manifest::find_by_root_commit(const std::string& rootCommit) {
    if (rootCommit.empty()) return nullptr;
    for (auto& e : entries_) {
        if (!e.rootCommit.empty() && e.rootCommit == rootCommit) return &e;
    }
    return nullptr;
}

Entry* Manifest::find_by_remote(const std::string& normalizedRemote) {
    if (normalizedRemote.empty()) return nullptr;
    for (auto& e : entries_) {
        for (const auto& r : e.remotes) {
            if (same_id(r, normalizedRemote)) return &e;
        }
    }
    return nullptr;
}

Entry* Manifest::resolve(const Project& p, Match* how) {
    auto set = [&](Match m) {
        if (how) *how = m;
    };

    if (Entry* e = find_by_id(p.id)) {
        set(Match::Id);
        return e;
    }
    if (Entry* e = find_by_root_commit(p.rootCommit)) {
        set(Match::RootCommit);
        return e;
    }
    if (Entry* e = find_by_remote(normalize_remote(p.remoteUrl))) {
        set(Match::RemoteAlias);
        return e;
    }

    set(Match::None);
    return nullptr;
}

Entry& Manifest::upsert(const Project& p, const std::string& machine, bool touched) {
    Match how = Match::None;
    Entry* found = resolve(p, &how);

    std::string now = now_iso8601();
    std::string folder = p.cwd.filename().string();
    bool isNew = (found == nullptr);

    if (isNew) {
        Entry e;
        e.id = p.id;
        e.rootCommit = p.rootCommit;
        e.canonicalSince = now;
        entries_.push_back(std::move(e));
        std::sort(entries_.begin(), entries_.end(),
                  [](const Entry& a, const Entry& b) { return a.id < b.id; });
        found = find_by_id(p.id);
    }

    // A rewritten history changes the root commit, and the remote alias is what
    // matched instead. Take the new one rather than keeping a value that no
    // longer exists in the repo.
    if (!p.rootCommit.empty()) found->rootCommit = p.rootCommit;

    push_unique(found->remotes, normalize_remote(p.remoteUrl));
    push_unique(found->remotes, strip_remote_prefix(found->id));
    push_front_unique(found->names, folder);

    if (found->canonicalSince.empty()) found->canonicalSince = now;

    // Timestamps only move when something really happened, or when this machine
    // is introducing itself. Otherwise every idle sync would dirty the file.
    if (isNew || touched || !found->machines.count(machine)) {
        found->machines[machine] = now;
        found->updated = now;
    }

    return *found;
}

void Manifest::merge_from(const Manifest& other) {
    for (const auto& theirs : other.entries_) {
        Entry* ours = find_by_id(theirs.id);
        if (!ours) {
            entries_.push_back(theirs);
            continue;
        }

        for (const auto& r : theirs.remotes) push_unique(ours->remotes, r);
        for (auto it = theirs.names.rbegin(); it != theirs.names.rend(); ++it) {
            push_front_unique(ours->names, *it);
        }

        for (const auto& [machine, when] : theirs.machines) {
            auto found = ours->machines.find(machine);
            if (found == ours->machines.end() || found->second < when) {
                ours->machines[machine] = when;
            }
        }

        // The side that wrote more recently has the better view of the root
        // commit, which is the only field where the two can genuinely disagree.
        if (theirs.updated > ours->updated && !theirs.rootCommit.empty()) {
            ours->rootCommit = theirs.rootCommit;
        }
        if (theirs.updated > ours->updated) ours->updated = theirs.updated;

        // canonicalSince marks when this id became current. The earlier
        // observation is the true one; taking the later would let a device
        // reset the clock that stops stale devices renaming backwards.
        if (!theirs.canonicalSince.empty() &&
            (ours->canonicalSince.empty() || theirs.canonicalSince < ours->canonicalSince)) {
            ours->canonicalSince = theirs.canonicalSince;
        }
    }

    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.id < b.id; });
}

}  // namespace claude_sync
