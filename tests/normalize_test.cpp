#include <iostream>
#include <string>
#include <vector>

#include "project.h"

using csync::normalize_remote;
using csync::same_id;

namespace {

int failures = 0;

void check(const std::string& input, const std::string& expected) {
    std::string got = normalize_remote(input);
    if (got != expected) {
        std::cerr << "FAIL  " << (input.empty() ? "(empty)" : input) << "\n"
                  << "  expected: " << expected << "\n"
                  << "  got:      " << got << "\n";
        ++failures;
    }
}

void check_true(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "FAIL  " << what << "\n";
        ++failures;
    }
}

}  // namespace

int main() {
    // The four forms actually present on this machine.
    check("https://github.com/abueelo/portfolio.git", "github.com/abueelo/portfolio");
    check("https://github.com/abueelo/Automated-Bulb-Release.git",
          "github.com/abueelo/Automated-Bulb-Release");
    check("git@github.com:abueelo/MegaMouse.git", "github.com/abueelo/MegaMouse");
    check("https://abueelo@github.com/t4t45123/Microvision.git",
          "github.com/t4t45123/Microvision");

    // ssh:// and git:// schemes.
    check("ssh://git@github.com/o/r.git", "github.com/o/r");
    check("git://github.com/o/r.git", "github.com/o/r");
    check("http://github.com/o/r", "github.com/o/r");

    // The same repo reached four ways must collapse to one identity.
    const std::string want = "github.com/abueelo/portfolio";
    check("https://github.com/abueelo/portfolio", want);
    check("git@github.com:abueelo/portfolio.git", want);
    check("ssh://git@github.com/abueelo/portfolio.git", want);
    check("https://abueelo@github.com/abueelo/portfolio.git/", want);

    // Trailing slashes, repeated slashes, whitespace.
    check("https://github.com/o/r/", "github.com/o/r");
    check("https://github.com//o//r.git", "github.com/o/r");
    check("  https://github.com/o/r.git  ", "github.com/o/r");

    // Ports are transport detail, not identity.
    check("ssh://git@github.com:2222/o/r.git", "github.com/o/r");
    check("ssh://github.com:22/o/r", "github.com/o/r");

    // Host case is normalized; owner and repo keep the case they were seen with.
    check("https://GitHub.COM/Abueelo/Portfolio.git", "github.com/Abueelo/Portfolio");

    // Self-hosted and non-GitHub remotes still resolve.
    check("git@gitlab.internal.example:team/tool.git", "gitlab.internal.example/team/tool");
    check("https://bitbucket.org/o/r.git", "bitbucket.org/o/r");

    // A repo whose name genuinely ends in .git keeps its name intact only when
    // spelled with the double suffix; a single suffix is always stripped.
    check("https://github.com/o/dotgit.git", "github.com/o/dotgit");

    // Local paths have no host, so they cannot serve as a shared identity.
    check("/Users/thom/Desktop/thing", "/Users/thom/Desktop/thing");

    check("", "");
    check("   ", "");

    // Case-insensitive matching keeps two spellings from minting two entries.
    check_true(same_id("remote/github.com/abueelo/Portfolio", "remote/github.com/abueelo/portfolio"),
               "same_id should ignore case");
    check_true(!same_id("remote/github.com/a/one", "remote/github.com/a/two"),
               "same_id should still separate different repos");

    if (failures == 0) {
        std::cout << "all normalize tests passed\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed\n";
    return 1;
}
