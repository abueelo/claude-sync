#pragma once

#include <string>
#include <vector>

#include "project.h"

namespace csync {

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
    Scope scope;
    std::vector<std::string> exclude;
};

Config default_config();
Config load_config();
bool save_config(const Config& c);
bool config_exists();

// The resolution cache. The per-file manifest the full design calls for arrives
// with mirroring in step 2; recording it now would be describing files this
// version never reads.
bool save_state(const std::vector<Project>& projects);

std::string hostname();

}  // namespace csync
