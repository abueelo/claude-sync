#pragma once

#include <string>

namespace claude_sync {

// Reads a password from the terminal with echo disabled. Returns false if
// there is no terminal -- which is the case inside a hook, and is exactly why
// the key is cached rather than asked for on every run.
bool prompt_password(const std::string& prompt, std::string& out);

bool have_tty();

// Yes/no on stdin, defaulting to `defaultYes` on a bare newline.
bool prompt_yes_no(const std::string& question, bool defaultYes);

}  // namespace claude_sync
