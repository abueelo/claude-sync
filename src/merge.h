#pragma once

#include <string>

namespace claude_sync {

// MEMORY.md is an index of "- [Title](file.md) — hook" lines, one per memory.
// Two machines writing different memories produce two different indexes, and a
// line-based merge either conflicts or drops entries. Neither is acceptable for
// a file whose whole job is to list what exists.
//
// The union is keyed on the link target, since that is the memory's identity --
// the title and hook are prose that can be reworded. Our ordering is kept and
// theirs appended, so the file stays stable for whoever is reading it locally.
// Non-index lines (headings, blank lines, prose) pass through from our side.
//
// A key present in the base but gone from one side is a real deletion and is
// not resurrected from the other side.
std::string merge_memory_index(const std::string& base, const std::string& ours,
                               const std::string& theirs);

// Returns the link target of an index line, or empty if the line is not one.
std::string index_line_key(const std::string& line);

}  // namespace claude_sync
