#!/bin/sh
# Fetches the claude-sync binary on SessionStart, then runs the session-start
# hook. This is the only place a download happens: there is no install-time
# lifecycle hook in the plugin system, so SessionStart is the earliest point
# any code runs at all. Stop and SessionEnd call the cached binary directly
# with no wrapper in between, so the per-turn hot path never pays for this.
#
# The binary is cached under CLAUDE_PLUGIN_DATA rather than CLAUDE_PLUGIN_ROOT
# because that directory survives a plugin update -- upgrading the plugin
# should not force a re-download unless the pinned binary version changed.
set -eu

PLUGIN_ROOT="${CLAUDE_PLUGIN_ROOT:?CLAUDE_PLUGIN_ROOT not set}"
DATA_DIR="${CLAUDE_PLUGIN_DATA:?CLAUDE_PLUGIN_DATA not set}"
BIN_DIR="$DATA_DIR/bin"
BIN="$BIN_DIR/claude-sync"
VERSION_FILE="$DATA_DIR/bin-version"
MANIFEST="$PLUGIN_ROOT/.claude-plugin/plugin.json"

# Never a fatal error: a session must start even if the manifest is somehow
# unreadable. Worst case, the version check below always looks stale and it
# redownloads every session, which is safe, just wasteful.
WANT_VERSION=$(grep -o '"version"[[:space:]]*:[[:space:]]*"[^"]*"' "$MANIFEST" 2>/dev/null \
  | sed 's/.*"\([^"]*\)"$/\1/' || echo "unknown")

# The repo that publishes release binaries. Override for local testing.
RELEASE_REPO="${CLAUDE_SYNC_RELEASE_REPO:-abueelo/claude-sync}"

need_fetch=1
if [ -x "$BIN" ] && [ -f "$VERSION_FILE" ] && [ "$(cat "$VERSION_FILE" 2>/dev/null)" = "$WANT_VERSION" ]; then
    need_fetch=0
fi

if [ "$need_fetch" = "1" ]; then
    mkdir -p "$BIN_DIR"

    # A local build always wins over a network fetch -- this is what makes the
    # bootstrap path testable without a real release, and is also the escape
    # hatch for someone who built claude-sync themselves.
    if [ -n "${CLAUDE_SYNC_LOCAL_BIN:-}" ] && [ -x "${CLAUDE_SYNC_LOCAL_BIN:-}" ]; then
        cp "$CLAUDE_SYNC_LOCAL_BIN" "$BIN.tmp"
    else
        case "$(uname -s)-$(uname -m)" in
            Linux-x86_64)              asset=claude-sync-linux-x86_64 ;;
            Linux-aarch64|Linux-arm64) asset=claude-sync-linux-arm64 ;;
            Darwin-*)                  asset=claude-sync-macos-universal ;;
            *)
                echo "claude-sync: no release binary for $(uname -s)-$(uname -m); memory sync is unavailable this session" >&2
                exit 0
                ;;
        esac

        url="https://github.com/$RELEASE_REPO/releases/download/v$WANT_VERSION/$asset"
        if ! curl -fsSL -o "$BIN.tmp" "$url" 2>/dev/null; then
            echo "claude-sync: could not fetch $url; memory sync is unavailable this session" >&2
            rm -f "$BIN.tmp"
            exit 0
        fi
    fi

    chmod +x "$BIN.tmp"
    mv "$BIN.tmp" "$BIN"
    echo "$WANT_VERSION" > "$VERSION_FILE"
fi

if [ ! -x "$BIN" ]; then
    exit 0
fi

# exec, not a subshell call, so the hook's stdin (the SessionStart JSON
# payload) reaches claude-sync directly rather than being consumed here.
exec "$BIN" hook session-start
