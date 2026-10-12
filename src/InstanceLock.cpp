#include "InstanceLock.h"
#include "Utils.h"
#ifdef _WIN32
#include <windows.h>
struct InstanceLock::Impl
{
    HANDLE h = INVALID_HANDLE_VALUE;
};
bool InstanceLock::tryAcquire()
{
    if (impl)
        return true;
    auto path = (Utils::dataDir() / ".securevault.lock").wstring();
    // Exclusive share mode: a second CreateFile on the same file fails while this handle is open.
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    impl = new Impl{h};
    return true;
}
InstanceLock::~InstanceLock()
{
    if (impl)
    {
        CloseHandle(impl->h);
        delete impl;
    }
}
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
struct InstanceLock::Impl
{
    int fd = -1;
};
bool InstanceLock::tryAcquire()
{
    if (impl)
        return true;
    auto path = (Utils::dataDir() / ".securevault.lock").string();
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        ::close(fd);
        return false;
    }
    impl = new Impl{fd};
    return true;
}
InstanceLock::~InstanceLock()
{
    if (impl)
    {
        ::flock(impl->fd, LOCK_UN);
        ::close(impl->fd);
        delete impl;
    }
}
#endif
