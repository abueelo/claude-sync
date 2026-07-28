#include "lock.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <string>

namespace csync {

Lock::~Lock() { release(); }

bool Lock::acquire(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    fd_ = ::open(path.string().c_str(), O_CREAT | O_RDWR, 0644);
    if (fd_ < 0) return false;

    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    path_ = path;

    // Recording the pid makes a stale lock diagnosable. The lock itself is held
    // by flock, which the kernel releases if we die, so this is only a note.
    std::string pid = std::to_string(::getpid()) + "\n";
    if (::ftruncate(fd_, 0) == 0) {
        ssize_t n = ::write(fd_, pid.data(), pid.size());
        (void)n;
    }
    return true;
}

void Lock::release() {
    if (fd_ < 0) return;
    ::flock(fd_, LOCK_UN);
    ::close(fd_);
    fd_ = -1;
}

}  // namespace csync
