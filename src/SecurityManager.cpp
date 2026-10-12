#include "SecurityManager.h"
#include <cstdlib>
std::int64_t SecurityManager::nowTicks()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
SecurityManager::SecurityManager(int autoLockSeconds) : lastTicks(nowTicks()), limitSeconds(autoLockSeconds)
{
#ifndef NDEBUG
    if (const char *e = std::getenv("SECUREVAULT_AUTOLOCK_SECONDS"))
    {
        int v = std::atoi(e);
        if (v > 0)
            limitSeconds = v;
    }
#endif
}
void SecurityManager::touch() { lastTicks = nowTicks(); }
int SecurityManager::idleSeconds() const { return (int)((nowTicks() - lastTicks.load()) / 1000); }
void SecurityManager::resetFailures()
{
    failedAttempts = 0;
    lockoutUntilTicks = 0;
}
bool SecurityManager::recordFailure(int maxAttempts)
{
    int n = ++failedAttempts;
    int wait = n <= 2 ? 0 : n == 3 ? 5 : n == 4 ? 10 : n == 5 ? 30 : 60;
    lockoutUntilTicks = wait ? nowTicks() + wait * 1000 : 0;
    return n >= maxAttempts;
}
int SecurityManager::lockoutRemaining() const
{
    std::int64_t left = lockoutUntilTicks.load() - nowTicks();
    return left <= 0 ? 0 : (int)((left + 999) / 1000);
}
