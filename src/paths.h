#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace csync {

fs::path home_dir();

// $HOME/.claude, overridable with CSYNC_CLAUDE_DIR so tests can point at a
// fixture tree instead of the real one.
fs::path claude_dir();

fs::path projects_dir();
fs::path csync_dir();

// Every directory under ~/.claude/projects/, sorted by name.
std::vector<fs::path> list_project_dirs();

// Recovers the real working directory from the session transcripts. The escaped
// directory name is lossy -- underscores, spaces and separators all collapse to
// '-' -- so the only reliable source is the "cwd" field inside the .jsonl files.
// Empty if no transcript yields one.
std::string cwd_for_project_dir(const fs::path& project_dir);

int count_memory_files(const fs::path& project_dir);

// Writes via a sibling .tmp file and renames over the target.
bool write_atomic(const fs::path& target, const std::string& contents);

}  // namespace csync
