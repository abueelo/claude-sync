# claude-sync

Syncs Claude Code's per-project memory (`~/.claude/projects/*/memory/`) and global `CLAUDE.md`/`skills`/`agents`/`commands` across machines, through a private git repo you control.

Every other tool in this space identifies a project by its escaped directory name (`~/Desktop/portfolio_site` → `-Users-thom-Desktop-portfolio-site`), which only works if every machine mounts the repo at an identical path. claude-sync identifies a project by its git remote instead, so renaming a folder, moving it, or checking it out somewhere else on a second machine doesn't lose the link.

## How identity works

- Git repo with a remote → `remote/<host>/<owner>/<repo>`, from the normalized URL
- Git repo, no remote → `local/<root commit prefix>`
- No git repo → not synced

A project's root commit is recorded either way, and it's the join key that survives a rename, a GitHub transfer, or a remote URL switching between `ssh` and `https`. When a project's identity changes, claude-sync moves its memory with a `git mv` instead of starting a new entry, and keeps the old identity as an alias so a device that hasn't caught up on the rename yet still resolves correctly rather than fighting over the folder name.

## Setup

```
claude-sync init --remote <url>
```

The remote is a private repo you create — `claude-sync` never touches GitHub before you give it a URL. You'll be asked whether to encrypt it; say yes unless you have a specific reason not to.

Then wire up the hooks (see below), or call `claude-sync sync` yourself whenever.

## Encryption

The sync repo can hold everything in the clear or fully sealed — contents, filenames, and the directory structure itself, since a project's path in the repo is literally the name of a private repo you work on. There's no partial option: it's all or nothing, decided once at `init`.

The encryption is deterministic — the same memory always encrypts to the same bytes. That's a deliberate tradeoff: it's what lets an unchanged file produce no git diff, so idle syncs don't spam the history with commits. The cost is that someone with the repo can tell when two files are byte-identical, or that a file was reverted to an earlier value. Nothing else about the content or the fact that two files match is exposed.

The key is derived from a password via Argon2id and cached at `~/.claude/claude-sync/key` (mode 0600). That file is not itself encrypted — your memory already sits in plaintext at `~/.claude/projects/`, so encrypting the cache would protect nothing that isn't already exposed on this machine. The password's job is to keep the *remote* copy unreadable to anyone who isn't you.

Lose the password and the synced memory is unrecoverable — there's no reset. On a new device, run `claude-sync init --remote <url>` and enter the same password when prompted.

## Hooks

Point Claude Code's `SessionStart`, `Stop`, and `SessionEnd` hooks at `claude-sync hook <event>`. A hook always exits `0` no matter what happens — a network failure, a locked repo, a missing key — because a sync problem should never be the reason a session won't start. Errors land in `~/.claude/claude-sync/claude-sync.log`.

The `Stop` hook fires after every assistant turn, so it checks memory-file timestamps before doing anything else and returns immediately if nothing changed.

## Commands

```
claude-sync init [--remote URL] [--encrypt|--no-encrypt] [--create-remote NAME]
claude-sync status [--json]
claude-sync pull / push / sync [--dry-run]
claude-sync unlock
claude-sync hook <event>
```

## Building

C++17, CMake ≥ 3.20, no package manager. `git` and (optionally) `gh` are shelled out to; everything else is vendored in `third_party/` (nlohmann/json, Monocypher).

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Platforms

Linux (x86_64, arm64) and macOS (universal) are built and tested in CI. Windows is not — the implementation shells out through POSIX APIs (`popen`, `flock`, `termios`, `/dev/urandom`) in most of the core, and porting that to native Win32 equivalents hasn't been done or tested. Release binaries only cover the three platforms above.
