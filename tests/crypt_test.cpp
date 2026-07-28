#include <cstdlib>
#include <iostream>
#include <string>

#include "crypt.h"
#include "paths.h"

using namespace claude_sync;

namespace {

int failures = 0;

void ok(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "FAIL  " << what << "\n";
        ++failures;
    }
}

// Argon2 at the shipped settings takes long enough that running it per test
// would dominate the suite. The properties under test do not depend on the
// cost parameters, so the tests use cheap ones.
CryptHeader cheap_header() {
    CryptHeader h = new_header();
    h.blocks = 64;
    h.passes = 1;
    return h;
}

}  // namespace

int main() {
    CryptHeader h = cheap_header();
    ok(h.valid(), "new_header produces a salt");

    Keys k = derive_keys("correct horse battery staple", h);
    Keys same = derive_keys("correct horse battery staple", h);
    Keys other = derive_keys("wrong password", h);

    ok(k.content == same.content, "same password and salt derive the same key");
    ok(k.content != other.content, "a different password derives a different key");
    ok(k.content != k.path && k.path != k.nonce && k.nonce != k.verifier,
       "subkeys are distinct from one another");
    ok(verifier_hex(k) != verifier_hex(other), "the verifier catches a wrong password");

    // Two salts must not collide.
    CryptHeader h2 = cheap_header();
    ok(h.salt != h2.salt, "salts are random");
    ok(derive_keys("correct horse battery staple", h2).content != k.content,
       "the same password under a different salt gives a different key");

    // Round trip.
    {
        std::string path = "projects/remote/github.com/o/r/memory/MEMORY.md";
        std::string body = "- [A](a.md) — a note\n";
        std::string sealed = seal(k, path, body);

        std::string gotPath, gotBody;
        ok(open_sealed(k, sealed, gotPath, gotBody), "round trip opens");
        ok(gotPath == path, "the embedded path survives");
        ok(gotBody == body, "the contents survive");

        // The embedded path is what lets the merge driver recognise MEMORY.md
        // when the filename on disk is a hash.
        ok(gotPath.rfind("MEMORY.md") == gotPath.size() - 9,
           "the driver can identify MEMORY.md from the envelope");
    }

    // Determinism: this is what stops an idle sync from producing a commit.
    {
        std::string a = seal(k, "p/x.md", "same bytes");
        std::string b = seal(k, "p/x.md", "same bytes");
        ok(a == b, "sealing identical input twice is byte-identical");

        ok(seal(k, "p/x.md", "same bytes") != seal(k, "p/y.md", "same bytes"),
           "the same content at a different path seals differently");
        ok(seal(k, "p/x.md", "one") != seal(k, "p/x.md", "two"),
           "different content seals differently");
    }

    // Wrong key must refuse rather than return garbage.
    {
        std::string sealed = seal(k, "p/x.md", "secret");
        std::string p, c;
        ok(!open_sealed(other, sealed, p, c), "a wrong key fails to open");
    }

    // Tampering must be caught by the MAC.
    {
        std::string sealed = seal(k, "p/x.md", "secret");
        sealed[sealed.size() - 1] ^= 0x01;
        std::string p, c;
        ok(!open_sealed(k, sealed, p, c), "a flipped ciphertext bit is rejected");
    }
    {
        std::string sealed = seal(k, "p/x.md", "secret");
        sealed[10] ^= 0x01;  // inside the nonce
        std::string p, c;
        ok(!open_sealed(k, sealed, p, c), "a flipped nonce bit is rejected");
    }
    {
        std::string p, c;
        ok(!open_sealed(k, "not an envelope at all", p, c), "garbage input is rejected");
        ok(!open_sealed(k, "", p, c), "empty input is rejected");
    }

    // Empty contents are legitimate -- a memory file can be truncated.
    {
        std::string sealed = seal(k, "p/empty.md", "");
        std::string p, c;
        ok(open_sealed(k, sealed, p, c), "an empty file round trips");
        ok(p == "p/empty.md" && c.empty(), "an empty file keeps its path");
    }

    // Binary-safe: memory files are text today, but nothing should depend on it.
    {
        std::string body("\x00\x01\xff\xfe binary \n\0 bytes", 22);
        std::string sealed = seal(k, "p/bin", body);
        std::string p, c;
        ok(open_sealed(k, sealed, p, c) && c == body, "binary content round trips");
    }

    // Path hashing: deterministic, distinct, and revealing nothing.
    {
        std::string a = hash_path(k, "remote/github.com/abueelo/portfolio");
        std::string b = hash_path(k, "remote/github.com/abueelo/portfolio");
        std::string c = hash_path(k, "remote/github.com/abueelo/other");
        ok(a == b, "path hashing is deterministic");
        ok(a != c, "different paths hash differently");
        ok(a.find("portfolio") == std::string::npos && a.find("github") == std::string::npos,
           "the hashed path leaks no plaintext");
        ok(hash_path(other, "remote/github.com/abueelo/portfolio") != a,
           "path hashing is keyed, so it differs per password");
    }

    // The envelope must not leak the path in the clear either.
    {
        std::string sealed = seal(k, "projects/remote/github.com/abueelo/portfolio/memory/x.md",
                                  "deploys run from main");
        ok(sealed.find("portfolio") == std::string::npos, "the envelope hides the path");
        ok(sealed.find("github") == std::string::npos, "the envelope hides the host");
        ok(sealed.find("deploys") == std::string::npos, "the envelope hides the contents");
        ok(sealed.compare(0, 6, "CSYNC1") == 0, "the envelope is recognisable by its magic");
    }

    // Key file round trip, and the permissions it claims to set.
    {
        const char* tmp = std::getenv("TMPDIR");
        fs::path dir = fs::path(tmp ? tmp : "/tmp") / "claude-sync-crypt-test";
        std::error_code ec;
        fs::remove_all(dir, ec);

        ok(save_keys(dir, k), "keys save");
        ok(keys_cached(dir), "cached keys are detected");

        Keys loaded;
        ok(load_keys(dir, loaded), "keys load");
        ok(loaded.content == k.content && loaded.path == k.path && loaded.nonce == k.nonce,
           "loaded keys match what was saved");

        auto perms = fs::status(dir / "key", ec).permissions();
        ok((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none,
           "the key file is not readable by group or others");

        ok(forget_keys(dir), "keys can be forgotten");
        ok(!keys_cached(dir), "forgotten keys are gone");
        fs::remove_all(dir, ec);
    }

    if (failures == 0) {
        std::cout << "all crypt tests passed\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed\n";
    return 1;
}
