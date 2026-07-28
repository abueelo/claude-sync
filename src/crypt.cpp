#include "crypt.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <vector>

#include "json.hpp"
#include "paths.h"

extern "C" {
#include "monocypher.h"
}

using nlohmann::json;

namespace claude_sync {
namespace {

constexpr char kMagic[6] = {'C', 'S', 'Y', 'N', 'C', '1'};
constexpr size_t kMagicLen = 6;
constexpr size_t kNonceLen = 24;
constexpr size_t kMacLen = 16;
constexpr size_t kHeaderLen = kMagicLen + kNonceLen + kMacLen;

fs::path key_path(const fs::path& dir) { return dir / "key"; }

void subkey(Key& out, const uint8_t* master, const char* purpose) {
    crypto_blake2b_keyed(out.data(), out.size(), master, 32,
                         reinterpret_cast<const uint8_t*>(purpose), std::strlen(purpose));
}

// Little-endian varint, so a path length costs one byte in the normal case.
void put_varint(std::string& out, uint64_t v) {
    while (v >= 0x80) {
        out += static_cast<char>((v & 0x7f) | 0x80);
        v >>= 7;
    }
    out += static_cast<char>(v);
}

bool get_varint(const std::string& in, size_t& pos, uint64_t& v) {
    v = 0;
    int shift = 0;
    while (pos < in.size()) {
        uint8_t b = static_cast<uint8_t>(in[pos++]);
        v |= static_cast<uint64_t>(b & 0x7f) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
        if (shift > 63) return false;
    }
    return false;
}

bool random_bytes(uint8_t* out, size_t len) {
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    size_t got = 0;
    while (got < len) {
        ssize_t n = ::read(fd, out + got, len - got);
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        got += static_cast<size_t>(n);
    }
    ::close(fd);
    return true;
}

}  // namespace

std::string to_hex(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0x0f];
    }
    return out;
}

bool from_hex(const std::string& hex, uint8_t* out, size_t len) {
    if (hex.size() != len * 2) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < len; ++i) {
        int hi = nibble(hex[i * 2]);
        int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

CryptHeader new_header() {
    CryptHeader h;
    uint8_t salt[16];
    if (!random_bytes(salt, sizeof salt)) return h;  // salt stays empty -> invalid
    h.salt = to_hex(salt, sizeof salt);
    return h;
}

bool header_exists(const fs::path& repo) {
    std::error_code ec;
    return fs::is_regular_file(repo / "crypt.json", ec);
}

bool read_header(const fs::path& repo, CryptHeader& out) {
    std::string text = read_file(repo / "crypt.json");
    if (text.empty()) return false;

    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;

    out.version = j.value("version", 1);
    out.salt = j.value("salt", std::string{});
    out.verifier = j.value("verifier", std::string{});
    out.blocks = j.value("blocks", 65536u);
    out.passes = j.value("passes", 3u);
    out.lanes = j.value("lanes", 1u);
    return out.valid();
}

bool write_header(const fs::path& repo, const CryptHeader& h) {
    json j;
    j["version"] = h.version;
    j["kdf"] = "argon2id";
    j["salt"] = h.salt;
    j["verifier"] = h.verifier;
    j["blocks"] = h.blocks;
    j["passes"] = h.passes;
    j["lanes"] = h.lanes;
    return write_atomic(repo / "crypt.json", j.dump(2) + "\n");
}

Keys derive_keys(const std::string& password, const CryptHeader& h) {
    Keys k;

    std::vector<uint8_t> salt(16, 0);
    if (!from_hex(h.salt, salt.data(), salt.size())) return k;

    // Argon2 wants nb_blocks * 1 KiB of scratch space.
    std::vector<uint8_t> work(static_cast<size_t>(h.blocks) * 1024);

    crypto_argon2_config config{};
    config.algorithm = CRYPTO_ARGON2_ID;
    config.nb_blocks = h.blocks;
    config.nb_passes = h.passes;
    config.nb_lanes = h.lanes;

    crypto_argon2_inputs inputs{};
    inputs.pass = reinterpret_cast<const uint8_t*>(password.data());
    inputs.pass_size = static_cast<uint32_t>(password.size());
    inputs.salt = salt.data();
    inputs.salt_size = static_cast<uint32_t>(salt.size());

    uint8_t master[32];
    crypto_argon2(master, sizeof master, work.data(), config, inputs, crypto_argon2_no_extras);

    crypto_wipe(work.data(), work.size());

    subkey(k.content, master, "claude-sync content");
    subkey(k.path, master, "claude-sync path");
    subkey(k.nonce, master, "claude-sync nonce");
    subkey(k.verifier, master, "claude-sync verify");

    crypto_wipe(master, sizeof master);
    return k;
}

std::string verifier_hex(const Keys& k) { return to_hex(k.verifier.data(), k.verifier.size()); }

bool save_keys(const fs::path& dir, const Keys& k) {
    std::error_code ec;
    fs::create_directories(dir, ec);

    fs::path p = key_path(dir);

    // Created with 0600 from the start rather than chmod'ed afterwards, so the
    // key is never briefly world-readable.
    int fd = ::open(p.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;

    std::string blob;
    blob.reserve(4 * 64 + 4);
    blob += to_hex(k.content.data(), k.content.size()) + "\n";
    blob += to_hex(k.path.data(), k.path.size()) + "\n";
    blob += to_hex(k.nonce.data(), k.nonce.size()) + "\n";
    blob += to_hex(k.verifier.data(), k.verifier.size()) + "\n";

    ssize_t n = ::write(fd, blob.data(), blob.size());
    ::close(fd);
    if (n != static_cast<ssize_t>(blob.size())) return false;

    ::chmod(p.string().c_str(), 0600);
    return true;
}

bool keys_cached(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(key_path(dir), ec);
}

bool load_keys(const fs::path& dir, Keys& out) {
    std::ifstream in(key_path(dir));
    if (!in) return false;

    std::string a, b, c, d;
    if (!std::getline(in, a) || !std::getline(in, b) || !std::getline(in, c) ||
        !std::getline(in, d)) {
        return false;
    }

    return from_hex(a, out.content.data(), out.content.size()) &&
           from_hex(b, out.path.data(), out.path.size()) &&
           from_hex(c, out.nonce.data(), out.nonce.size()) &&
           from_hex(d, out.verifier.data(), out.verifier.size());
}

bool forget_keys(const fs::path& dir) {
    std::error_code ec;
    return fs::remove(key_path(dir), ec);
}

std::string hash_path(const Keys& k, const std::string& plain) {
    uint8_t out[16];
    crypto_blake2b_keyed(out, sizeof out, k.path.data(), k.path.size(),
                         reinterpret_cast<const uint8_t*>(plain.data()), plain.size());
    return to_hex(out, sizeof out);
}

std::string seal(const Keys& k, const std::string& path, const std::string& contents) {
    std::string plain;
    plain.reserve(path.size() + contents.size() + 4);
    put_varint(plain, path.size());
    plain += path;
    plain += contents;

    // Synthetic nonce: same plaintext at the same path always yields the same
    // ciphertext, so an unchanged memory produces no git diff.
    uint8_t nonce[kNonceLen];
    {
        std::string material = path;
        material += '\0';
        material += plain;
        crypto_blake2b_keyed(nonce, sizeof nonce, k.nonce.data(), k.nonce.size(),
                             reinterpret_cast<const uint8_t*>(material.data()), material.size());
    }

    std::vector<uint8_t> cipher(plain.size());
    uint8_t mac[kMacLen];
    crypto_aead_lock(cipher.data(), mac, k.content.data(), nonce, nullptr, 0,
                     reinterpret_cast<const uint8_t*>(plain.data()), plain.size());

    std::string out;
    out.reserve(kHeaderLen + cipher.size());
    out.append(kMagic, kMagicLen);
    out.append(reinterpret_cast<const char*>(nonce), kNonceLen);
    out.append(reinterpret_cast<const char*>(mac), kMacLen);
    out.append(reinterpret_cast<const char*>(cipher.data()), cipher.size());
    return out;
}

bool open_sealed(const Keys& k, const std::string& sealed, std::string& pathOut,
                 std::string& contentsOut) {
    if (sealed.size() < kHeaderLen) return false;
    if (std::memcmp(sealed.data(), kMagic, kMagicLen) != 0) return false;

    const uint8_t* nonce = reinterpret_cast<const uint8_t*>(sealed.data()) + kMagicLen;
    const uint8_t* mac = nonce + kNonceLen;
    const uint8_t* cipher = mac + kMacLen;
    size_t cipherLen = sealed.size() - kHeaderLen;

    std::vector<uint8_t> plain(cipherLen);
    if (crypto_aead_unlock(plain.data(), mac, k.content.data(), nonce, nullptr, 0, cipher,
                           cipherLen) != 0) {
        return false;  // wrong key or tampered data
    }

    std::string text(reinterpret_cast<const char*>(plain.data()), plain.size());
    crypto_wipe(plain.data(), plain.size());

    size_t pos = 0;
    uint64_t pathLen = 0;
    if (!get_varint(text, pos, pathLen)) return false;
    if (pos + pathLen > text.size()) return false;

    pathOut = text.substr(pos, static_cast<size_t>(pathLen));
    contentsOut = text.substr(pos + static_cast<size_t>(pathLen));
    return true;
}

}  // namespace claude_sync
