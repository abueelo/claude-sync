---
description: Sync Claude Code memory with the private repo now
---

Run the cached claude-sync binary to sync memory immediately, rather than waiting for the next hook:

```bash
if [ -x "${CLAUDE_PLUGIN_DATA}/bin/claude-sync" ]; then
  "${CLAUDE_PLUGIN_DATA}/bin/claude-sync" sync
else
  echo "claude-sync has not been bootstrapped yet in this session. Start a new session and try again."
fi
```

Show the command's output to the user as-is. If it reports an encrypted repo with no cached key, tell the user to run `"${CLAUDE_PLUGIN_DATA}/bin/claude-sync" unlock` in a terminal — that prompt needs a real TTY and cannot run from inside this command.
