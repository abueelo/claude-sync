# claude-sync

Syncs Claude Code's per-project memory (`~/.claude/projects/*/memory/`) and global `CLAUDE.md`/`skills`/`agents`/`commands` across machines, through a private git repo you control.

It identifies a project by its git remote rather than its local folder path, so renaming or moving a project — or checking it out somewhere new on a second machine — doesn't lose the link to its memory.

## Getting started

### 1. Install the binary

**Plugin (recommended):**

```
/plugin marketplace add abueelo/claude-sync
/plugin install claude-sync@claude-sync
```

This wires up the sync hooks automatically and fetches the right binary for your platform on first use, into its own managed location — nothing for you to place by hand. Skip straight to step 2.

**Download a release binary:**

Grab the asset for your platform from the [latest release](https://github.com/abueelo/claude-sync/releases/latest) — `claude-sync-macos-universal`, `claude-sync-linux-x86_64`, or `claude-sync-linux-arm64` — then put it somewhere on your `PATH`:

```
curl -fsSL -o claude-sync https://github.com/abueelo/claude-sync/releases/latest/download/claude-sync-macos-universal
chmod +x claude-sync
sudo mv claude-sync /usr/local/bin/claude-sync
```

Swap the asset name for your platform. `/usr/local/bin` is on `PATH` by default on macOS and most Linux setups; use `~/.local/bin` instead if you'd rather not need `sudo` (make sure it's on your `PATH`).

**Build from source:**

```
git clone https://github.com/abueelo/claude-sync.git
cd claude-sync
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo mv build/claude-sync /usr/local/bin/claude-sync
```

### 2. Point it at a private repo

```
claude-sync init --remote <url>
```

`<url>` is a private git repo you create yourself — nothing here touches GitHub until you give it one. Don't have one yet? `claude-sync init --create-remote <name>` will create it for you via the `gh` CLI, if you have that installed and authenticated.

You'll be asked whether to encrypt the repo. Say yes unless you have a specific reason not to — see [Encryption](#encryption). On a second device, run the same command with the same password to unlock it.

### 3. Wire up the hooks

If you installed via the plugin, this is already done. Otherwise, point Claude Code's `SessionStart`, `Stop`, and `SessionEnd` hooks at `claude-sync hook <event>` in `settings.json`.

That's it — memory syncs automatically from here. A hook never blocks a session, even on failure; check `~/.claude/claude-sync/claude-sync.log` if something doesn't seem to be syncing.

## Commands

```
claude-sync init [--remote URL] [--encrypt|--no-encrypt] [--create-remote NAME]
claude-sync status [--json]
claude-sync pull / push / sync [--dry-run]
claude-sync unlock
claude-sync remove [--yes]
claude-sync hook <event>
```

## How identity works

- Git repo with a remote → `remote/<host>/<owner>/<repo>`, from the normalized URL
- Git repo, no remote → `local/<root commit prefix>`
- No git repo → not synced

The root commit is recorded either way and is what survives a rename, a GitHub transfer, or a remote URL switching between `ssh` and `https`. When a project's identity changes, claude-sync moves its memory instead of starting a new entry, and keeps the old identity as an alias so a device that hasn't caught up yet doesn't fight over the folder name.

## Encryption

Optional, decided once at `init`. When on, everything is sealed — contents, filenames, and the directory layout, since a project's path in the repo would otherwise reveal the name of a private repo you work on.

The password derives a key via Argon2id, cached locally. There's no recovery — lose the password and the synced memory is unreadable.

## Platforms

Linux (x86_64, arm64) and macOS (universal) are built and tested in CI. Windows isn't supported — most of the core shells out through POSIX APIs that haven't been ported.
