#pragma once
#include <string>
// One writer per data folder (P1-9). Held for the life of the object; released automatically if the
// process dies. Shared by the CLI and the GUI.
class InstanceLock
{
    struct Impl;
    Impl *impl = nullptr;
public:
    InstanceLock() = default;
    ~InstanceLock();
    InstanceLock(const InstanceLock &) = delete;
    InstanceLock &operator=(const InstanceLock &) = delete;
    // Locks <dataDir>/.securevault.lock. False when another instance holds it.
    bool tryAcquire();
    bool held() const { return impl != nullptr; }
};
