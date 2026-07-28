#include "password.h"

#include <termios.h>
#include <unistd.h>

#include <iostream>

namespace claude_sync {

bool have_tty() { return ::isatty(STDIN_FILENO) == 1; }

bool prompt_password(const std::string& prompt, std::string& out) {
    if (!have_tty()) return false;

    termios old{};
    if (::tcgetattr(STDIN_FILENO, &old) != 0) return false;

    termios noecho = old;
    noecho.c_lflag &= static_cast<tcflag_t>(~ECHO);
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &noecho) != 0) return false;

    std::cout << prompt << std::flush;
    bool ok = static_cast<bool>(std::getline(std::cin, out));

    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
    std::cout << "\n";

    return ok;
}

bool prompt_yes_no(const std::string& question, bool defaultYes) {
    if (!have_tty()) return defaultYes;

    std::cout << question << (defaultYes ? " [Y/n] " : " [y/N] ") << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer)) return defaultYes;

    if (answer.empty()) return defaultYes;
    return answer[0] == 'y' || answer[0] == 'Y';
}

}  // namespace claude_sync
