#include "setup.h"

#include <cstdlib>
#include <iostream>

#include "crypt.h"
#include "password.h"
#include "paths.h"
#include "repo.h"
#include "store.h"

namespace claude_sync {
namespace {

// Asks for a password and derives keys, checking against the header's verifier
// when there is one to check against.
// Without a terminal -- a scripted setup, or CI -- the password comes from the
// environment. It is never accepted from a command-line flag, which would put
// it in the shell history and the process list.
bool env_password(std::string& out) {
    const char* p = std::getenv("CLAUDE_SYNC_PASSWORD");
    if (!p || !*p) return false;
    out = p;
    return true;
}

bool ask_and_derive(const CryptHeader& h, bool confirm, Keys& out, std::string& err) {
    if (!have_tty()) {
        std::string password;
        if (!env_password(password)) {
            err = "no terminal to prompt for a password, and CLAUDE_SYNC_PASSWORD is not set";
            return false;
        }
        Keys k = derive_keys(password, h);
        if (!h.verifier.empty() && verifier_hex(k) != h.verifier) {
            err = "CLAUDE_SYNC_PASSWORD does not match this repo";
            return false;
        }
        out = k;
        return true;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        std::string password;
        if (!prompt_password("Password: ", password)) {
            err = "no terminal available to read a password";
            return false;
        }
        if (password.empty()) {
            std::cout << "Empty password. Try again.\n";
            continue;
        }

        if (confirm) {
            std::string again;
            if (!prompt_password("Confirm: ", again)) {
                err = "no terminal available to read a password";
                return false;
            }
            if (password != again) {
                std::cout << "Passwords did not match. Try again.\n";
                continue;
            }
        }

        std::cout << "Deriving key (this is deliberately slow)... " << std::flush;
        Keys k = derive_keys(password, h);
        std::cout << "done\n";

        if (!h.verifier.empty() && verifier_hex(k) != h.verifier) {
            std::cout << "That password does not match this repo. Try again.\n";
            continue;
        }

        out = k;
        return true;
    }

    err = "too many failed password attempts";
    return false;
}

}  // namespace

bool setup_encryption(Config& c, std::string& err, EncryptChoice choice) {
    if (!ensure_repo(c, err)) return false;

    fs::path repo = repo_path();

    if (header_exists(repo)) {
        CryptHeader h;
        if (!read_header(repo, h)) {
            err = "the repo has a crypt.json that could not be read";
            return false;
        }

        std::cout << "This sync repo is encrypted. Enter its password to unlock it here.\n";
        Keys k;
        if (!ask_and_derive(h, /*confirm=*/false, k, err)) return false;

        if (!save_keys(sync_dir_path(), k)) {
            err = "could not write the key file";
            return false;
        }
        c.encrypted = true;
        std::cout << "Unlocked. The key is cached at " << (sync_dir_path() / "key") << "\n";
        return true;
    }

    // No header: this repo has never been encrypted.
    bool wanted = choice == EncryptChoice::Yes ||
                  (choice == EncryptChoice::Ask && prompt_yes_no("Encrypt this sync repo?", true));
    if (!wanted) {
        c.encrypted = false;
        std::cout << "Continuing unencrypted. Memory will be readable by anyone who can\n"
                     "read the repo, including the names of the projects it came from.\n";
        return true;
    }

    std::cout << "\nChoose a password. You will need it on every other device.\n"
                 "There is no recovery -- lose it and the synced memory is unreadable.\n\n";

    CryptHeader h = new_header();
    if (!h.valid()) {
        err = "could not generate a salt";
        return false;
    }

    Keys k;
    if (!ask_and_derive(h, /*confirm=*/true, k, err)) return false;

    h.verifier = verifier_hex(k);
    if (!write_header(repo, h)) {
        err = "could not write crypt.json";
        return false;
    }
    if (!save_keys(sync_dir_path(), k)) {
        err = "could not write the key file";
        return false;
    }

    c.encrypted = true;

    // Commit the header immediately. If it only landed on the next sync, a
    // second device could clone in between, see no header, and set up a
    // conflicting password of its own.
    Store store;
    store.encrypted = true;
    store.keys = k;

    std::string pushErr;
    PushResult result;
    if (!repo_commit_and_push("Enable encryption", true, store, pushErr, &result)) {
        std::cout << "Note: the encryption header is written locally but could not be pushed ("
                  << pushErr << "). It will go up on the next sync.\n";
    }

    std::cout << "Encrypted. Key cached at " << (sync_dir_path() / "key") << "\n";
    return true;
}

bool unlock(Config& c, std::string& err) {
    if (!ensure_repo(c, err)) return false;

    fs::path repo = repo_path();
    if (!header_exists(repo)) {
        err = "this sync repo is not encrypted; there is nothing to unlock";
        return false;
    }

    CryptHeader h;
    if (!read_header(repo, h)) {
        err = "the repo has a crypt.json that could not be read";
        return false;
    }

    Keys k;
    if (!ask_and_derive(h, /*confirm=*/false, k, err)) return false;

    if (!save_keys(sync_dir_path(), k)) {
        err = "could not write the key file";
        return false;
    }

    c.encrypted = true;
    return true;
}

}  // namespace claude_sync
