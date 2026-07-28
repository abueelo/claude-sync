---
description: Show claude-sync's view of every project's identity and sync state
---

Run:

```bash
if [ -x "${CLAUDE_PLUGIN_DATA}/bin/claude-sync" ]; then
  "${CLAUDE_PLUGIN_DATA}/bin/claude-sync" status
else
  echo "claude-sync has not been bootstrapped yet in this session. Start a new session and try again."
fi
```

Show the table to the user as-is, without summarizing or reformatting it.
