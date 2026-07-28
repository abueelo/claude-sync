#include <iostream>
#include <string>

#include "merge.h"

using csync::index_line_key;
using csync::merge_memory_index;

namespace {

int failures = 0;

void eq(const std::string& what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::cerr << "FAIL  " << what << "\n----- got -----\n"
                  << got << "----- want ----\n"
                  << want << "---------------\n";
        ++failures;
    }
}

void contains(const std::string& what, const std::string& hay, const std::string& needle) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "FAIL  " << what << "\n  missing: " << needle << "\n  in:\n" << hay << "\n";
        ++failures;
    }
}

void absent(const std::string& what, const std::string& hay, const std::string& needle) {
    if (hay.find(needle) != std::string::npos) {
        std::cerr << "FAIL  " << what << "\n  should not contain: " << needle << "\n  in:\n"
                  << hay << "\n";
        ++failures;
    }
}

}  // namespace

int main() {
    // Key extraction.
    eq("key from a normal entry", index_line_key("- [Deploy notes](deploy.md) — how it goes out"),
       "deploy.md");
    eq("key with an em dash and nesting",
       index_line_key("  * [A](sub/dir/a.md) — note"), "sub/dir/a.md");
    eq("plus bullet", index_line_key("+ [A](a.md)"), "a.md");
    eq("prose is not an entry", index_line_key("This is a paragraph about [things](x.md)."), "");
    eq("heading is not an entry", index_line_key("# MEMORY"), "");
    eq("blank is not an entry", index_line_key(""), "");
    eq("bullet without a link", index_line_key("- just a bullet"), "");

    // The case this exists for: each side added a different memory.
    {
        std::string base = "- [Shared](shared.md) — common\n";
        std::string ours = "- [Shared](shared.md) — common\n- [Mine](mine.md) — from laptop\n";
        std::string theirs = "- [Shared](shared.md) — common\n- [Theirs](theirs.md) — from desktop\n";
        std::string got = merge_memory_index(base, ours, theirs);
        contains("union keeps ours", got, "(mine.md)");
        contains("union keeps theirs", got, "(theirs.md)");
        contains("union keeps the common entry", got, "(shared.md)");
        absent("no conflict markers", got, "<<<<<<<");
    }

    // Both added entries from an empty index.
    {
        std::string got = merge_memory_index("", "- [A](a.md) — a\n", "- [B](b.md) — b\n");
        contains("empty base keeps a", got, "(a.md)");
        contains("empty base keeps b", got, "(b.md)");
    }

    // A deletion on their side is honoured, not reverted.
    {
        std::string base = "- [A](a.md) — a\n- [Gone](gone.md) — g\n";
        std::string ours = "- [A](a.md) — a\n- [Gone](gone.md) — g\n";
        std::string theirs = "- [A](a.md) — a\n";
        std::string got = merge_memory_index(base, ours, theirs);
        absent("their delete sticks", got, "(gone.md)");
        contains("unrelated entry survives", got, "(a.md)");
    }

    // A deletion on our side is not resurrected by their copy.
    {
        std::string base = "- [A](a.md) — a\n- [Gone](gone.md) — g\n";
        std::string ours = "- [A](a.md) — a\n";
        std::string theirs = "- [A](a.md) — a\n- [Gone](gone.md) — g\n";
        std::string got = merge_memory_index(base, ours, theirs);
        absent("our delete sticks", got, "(gone.md)");
    }

    // Same target reworded on both sides: ours wins, and it appears once.
    {
        std::string base = "- [Old](a.md) — old\n";
        std::string ours = "- [Ours](a.md) — our wording\n";
        std::string theirs = "- [Theirs](a.md) — their wording\n";
        std::string got = merge_memory_index(base, ours, theirs);
        contains("our wording kept", got, "our wording");
        absent("their wording dropped", got, "their wording");
        eq("appears exactly once", std::to_string(got.find("a.md") != std::string::npos), "1");
    }

    // Non-entry lines pass through and additions land with the entries, not
    // after the trailing note.
    {
        std::string base = "# Index\n\n- [A](a.md) — a\n\nNotes below.\n";
        std::string ours = "# Index\n\n- [A](a.md) — a\n\nNotes below.\n";
        std::string theirs = "# Index\n\n- [A](a.md) — a\n- [B](b.md) — b\n\nNotes below.\n";
        std::string got = merge_memory_index(base, ours, theirs);
        contains("heading survives", got, "# Index");
        contains("trailing prose survives", got, "Notes below.");
        if (got.find("(b.md)") > got.find("Notes below.")) {
            std::cerr << "FAIL  addition landed after the trailing prose\n" << got << "\n";
            ++failures;
        }
    }

    // Identical sides are a no-op.
    {
        std::string same = "- [A](a.md) — a\n";
        eq("identical input unchanged", merge_memory_index(same, same, same), same);
    }

    if (failures == 0) {
        std::cout << "all merge tests passed\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed\n";
    return 1;
}
