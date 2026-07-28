#pragma once

#include <map>
#include <string>
#include <vector>

#include "project.h"

namespace claude_sync {

struct Scope {
    bool memory = true;
    bool globalClaudeMd = true;
    bool skills = true;
    bool agents = true;
    bool commands = true;
};

struct Config {
    std::string machineName;
    std::string remoteUrl;
    bool encrypted = false;
    Scope scope;
    std::vector<std::string> exclude;
};

Config default_config();
Config load_config();
bool save_config(const Config& c);
bool config_exists();

// What the last successful sync saw, keyed by project id then filename, with a
// content hash as the value. This is the third leg of the three-way compare:
// without it, a file present on one side and absent on the other is ambiguous
// between "newly added there" and "deleted here".
using FileSet = std::map<std::string, std::string>;
using Baseline = std::map<std::string, FileSet>;

// When this machine first observed a project's current remote, keyed by root
// commit because that survives the renames the remote does not.
//
// This is what the relink guard actually needs. Using wall-clock "now" as the
// observation time is wrong: a stale device's clock is always later than the
// rename it has not noticed, so the guard would never fire and the two devices
// would rename the project back and forth forever.
struct Observation {
    std::string remote;
    std::string firstSeen;
};

struct State {
    std::vector<Project> projects;
    Baseline baseline;
    std::map<std::string, Observation> observed;  // rootCommit -> observation
    std::string lastSync;
};

State load_state();
bool save_state(const State& s);

std::string hostname();

}  // namespace claude_sync
