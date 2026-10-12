#pragma once
#include "AuditLogger.h"
#include "FileVault.h"
#include "SecurityManager.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The unlocked state: username, vault key, vault, activity log, idle timer.
// No console, no Qt. The plaintext master password is never stored here.
class Session
{
    friend class AuthService;
    std::string user;
    std::vector<unsigned char> vaultKey;
    std::vector<unsigned char> salt;
    std::vector<unsigned char> verifier; // SHA-256 of the auth key, as stored in users.db
    std::uint32_t iterations = 0;
    std::unique_ptr<FileVault> vaultPtr;
    std::unique_ptr<AuditLogger> auditPtr;
    SecurityManager sec;

public:
    Session(std::string username, std::vector<unsigned char> vaultKey, std::vector<unsigned char> salt,
            std::vector<unsigned char> verifier, std::uint32_t iterations,
            int autoLockSeconds = SecurityManager::kDefaultAutoLockSeconds);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    const std::string &username() const { return user; }
    std::uint32_t kdfIterations() const { return iterations; }
    bool isLocked() const { return !vaultPtr; }
    // Throw std::logic_error after lock().
    FileVault &vault();
    AuditLogger &audit();
    SecurityManager &security() { return sec; }

    // Saves (if the vault is readable), writes the activity event, destroys the vault, zeroes the keys,
    // clears the clipboard if it still holds our text. Safe to call twice.
    void lock(const std::string &auditCode = "LOCK_VAULT", const std::string &detail = "Manual lock");
};
