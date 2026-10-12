#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
class SecurityManager
{
    std::atomic<std::int64_t> lastTicks;   // steady_clock ticks; atomic because a timer thread polls it
    std::atomic<std::int64_t> lockoutUntilTicks{0};
    std::atomic<int> failedAttempts{0};
    int limitSeconds;
    static std::int64_t nowTicks();
public:
    static constexpr int kDefaultAutoLockSeconds = 300;
    // In debug builds (NDEBUG not defined) SECUREVAULT_AUTOLOCK_SECONDS overrides the argument, for testing.
    explicit SecurityManager(int autoLockSeconds = kDefaultAutoLockSeconds);
    void touch();
    int idleSeconds() const;
    int autoLockSeconds() const { return limitSeconds; }
    bool timedOut() const { return idleSeconds() >= limitSeconds; }
    bool timedOut(int seconds) const { return idleSeconds() >= seconds; }
    void resetFailures();
    int failures() const { return failedAttempts; }
    // Records a failed sign-in and starts a cooldown (none for the first two failures, then 5, 10, 30, 60 s).
    // Returns true once maxAttempts failures have happened. The cooldown is per session, not a lockout.
    bool recordFailure(int maxAttempts = 5);
    int lockoutRemaining() const; // seconds, 0 when none
};
