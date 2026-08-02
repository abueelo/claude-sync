---
name: no-ai-attribution
description: "Never reference Claude/AI authorship anywhere in commits, code, comments, or files"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 62154719-e9d7-460f-be49-89a0d8e67064
---

Never add "Co-Authored-By: Claude" (or any Claude/Anthropic/AI-authorship reference) to commit messages, code comments, filenames, or any other file in a repo. Don't create files like `CLAUDE.md` that are named after or reference Claude specifically.

**Why:** User explicitly said (in caps, emphatically) to remove all Claude references from previous commits on the Cycle Tracker project and never put "Claude" anywhere. Had to rewrite git history (`git filter-branch --msg-filter`) to strip existing `Co-Authored-By: Claude Sonnet 5` trailers and force-push the cleaned history.

**How to apply:** Applies broadly, not just to this one project — when committing on behalf of this user, omit any AI co-author trailer entirely. If a repo scaffold generates an AI-tool-specific file (e.g. `CLAUDE.md`), remove it rather than leaving it in place.
