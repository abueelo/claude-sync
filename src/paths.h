#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace claude_sync {

fs::path home_dir();

// $HOME/.claude, overridable with CLAUDE_SYNC_CLAUDE_DIR so tests can point at a
// fixture tree instead of the real one.
fs::path claude_dir();

fs::path projects_dir();
fs::path sync_dir_path();

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

std::string read_file(const fs::path& p);

// Names of the regular files directly inside dir, sorted. Dotfiles skipped.
std::vector<std::string> list_files(const fs::path& dir);

// FNV-1a over the file's bytes, as 16 hex chars. Used only to answer "did this
// change since the last sync", so a non-cryptographic hash is the right tool.
std::string content_hash(const fs::path& p);
std::string hash_bytes(const std::string& data);

long long mtime_seconds(const fs::path& p);

bool copy_file_over(const fs::path& from, const fs::path& to);

std::string now_iso8601();

// Millisecond resolution. The relink guard compares timestamps for ordering,
// and two events inside the same second must not look simultaneous.
std::string now_iso8601_ms();

}  // namespace claude_sync
