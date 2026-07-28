#pragma once

#include <string>

namespace claude_sync {

// Runs a sync on behalf of a Claude Code hook.
//
// The one inviolable rule: this always reports success. A hook that fails is a
// session that will not start, and no sync problem is worth blocking a user's
// work over -- a network outage, a locked repo, a missing key all have to be
// survivable. Everything goes to claude-sync.log instead.
//
// `cwd` arrives on the hook payload, so the hot path never has to parse a
// session transcript to work out which project it is in.
int run_hook(const std::string& event);

void log_line(const std::string& message);

}  // namespace claude_sync
