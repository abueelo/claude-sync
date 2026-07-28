#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace claude_sync {

using Key = std::array<uint8_t, 32>;

// A private repo is not an unreadable one. Anyone who ends up with a copy --
// a compromised account, a machine handed on, a mis-set visibility -- can read
// every memory and, worse, the project ids, which are literally the names of
// the user's private repos. So the whole repo is encrypted, paths included.
struct Keys {
    Key content{};
    Key path{};
    Key nonce{};
    Key verifier{};
};

// The plaintext header at the repo root. A salt is not a secret, and a second
// device needs it to derive the same keys from the password alone.
struct CryptHeader {
    int version = 1;
    std::string salt;      // hex
    std::string verifier;  // hex, so a wrong password fails loudly
    uint32_t blocks = 65536;  // Argon2id memory, in KiB blocks
    uint32_t passes = 3;
    uint32_t lanes = 1;

    // A freshly minted header has a salt but no verifier yet -- the verifier
    // only exists once a password has been run through the KDF.
    bool valid() const { return !salt.empty(); }
};

bool read_header(const fs::path& repo, CryptHeader& out);
bool write_header(const fs::path& repo, const CryptHeader& h);
bool header_exists(const fs::path& repo);

CryptHeader new_header();

// Argon2id over the password, then one subkey per purpose. Slow by design --
// this runs once per device, not once per sync.
Keys derive_keys(const std::string& password, const CryptHeader& h);

std::string verifier_hex(const Keys& k);

// The cached master material, mode 0600. The plaintext memory already sits
// unencrypted in ~/.claude/projects, so a key file next to it gives away
// nothing that was not already there; the password's job is to protect the
// remote. Hooks run non-interactively and cannot prompt, so caching is a
// requirement rather than a shortcut.
bool save_keys(const fs::path& dir, const Keys& k);
bool load_keys(const fs::path& dir, Keys& out);
bool keys_cached(const fs::path& dir);
bool forget_keys(const fs::path& dir);

// Deterministic, so a name always maps to the same place in the repo and git
// still sees renames and history rather than a churn of unrelated blobs.
std::string hash_path(const Keys& k, const std::string& plain);

// Seals `contents` with the original `path` embedded INSIDE the ciphertext.
// The merge driver is handed blob content with a hashed filename, so the only
// way it can know it is holding MEMORY.md is if the name travels with the data.
//
// The nonce is synthetic -- derived from the plaintext rather than random --
// because a random nonce would re-encrypt identical memory to different bytes
// on every run, and git would see a change every sync. That would undo the
// no-op-sync-makes-no-commit property the Stop hook depends on. The cost is
// that an observer can tell when two files hold identical bytes.
std::string seal(const Keys& k, const std::string& path, const std::string& contents);

// Returns false on a bad key or tampered data. `pathOut` receives the embedded
// original path.
bool open_sealed(const Keys& k, const std::string& sealed, std::string& pathOut,
                 std::string& contentsOut);

std::string to_hex(const uint8_t* data, size_t len);
bool from_hex(const std::string& hex, uint8_t* out, size_t len);

}  // namespace claude_sync
