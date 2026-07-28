#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace csync {

// A non-blocking whole-process lock over ~/.claude/csync/csync.lock.
//
// Concurrent sessions are the normal case, not the exception -- several Claude
// windows can fire a hook at the same moment. A run that cannot take the lock
// skips rather than queues: the next hook will pick up whatever it missed, and
// blocking here would stall a session for no gain.
class Lock {
public:
    Lock() = default;
    ~Lock();

    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

    bool acquire(const fs::path& path);
    void release();

    bool held() const { return fd_ >= 0; }

private:
    int fd_ = -1;
    fs::path path_;
};

}  // namespace csync
