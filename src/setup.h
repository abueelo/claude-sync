#pragma once

#include <string>

#include "config.h"

namespace claude_sync {

// Ask is the interactive default; the explicit forms exist so a scripted setup
// never silently picks plaintext just because there is no terminal attached.
enum class EncryptChoice { Ask, Yes, No };

// Establishes encryption for the sync repo, interactively.
//
// Two situations, and telling them apart matters: a repo that already carries a
// crypt.json belongs to a device that set a password, so this machine must
// match it rather than invent a new one; a repo with no header has never been
// encrypted, so the user is asked whether to start now.
//
// A wrong password is caught by the verifier before anything is written, so the
// failure is "wrong password" rather than a repo full of undecryptable blobs.
bool setup_encryption(Config& c, std::string& err, EncryptChoice choice);

// Re-derives and caches the key on a device that has the repo but no key file.
bool unlock(Config& c, std::string& err);

}  // namespace claude_sync
