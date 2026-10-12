#pragma once
#include "Session.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Accounts and master-password workflows. No printing, no stdin; results are enums.
// Key derivation is slow (hundreds of ms): call from a worker thread in a GUI.
using ProgressFn = std::function<void(int done, int total)>;

enum class RegisterResult { Ok, InvalidUsername, UsernameTaken, WeakPassword, StorageError };
// VaultUnreadable: credentials were right but the vault file could not be read. `out` is still set,
// with a vault that refuses all writes, so the caller can offer restore().
enum class LoginResult { Ok, InvalidCredentials, VaultUnreadable, StorageError };
enum class ChangeResult { Ok, WrongPassword, WeakPassword, VaultNotReady, StorageError };
enum class LoginPurpose { SignIn, Unlock };

class AuthService
{
public:
    static constexpr std::uint32_t kNewAccountIterations = 600000;

    static bool isValidUsername(const std::string &name);
    static bool anyUserExists();
    static bool userExists(const std::string &name);
    // Iteration count stored for this account, 0 if there is no such account.
    static std::uint32_t kdfIterations(const std::string &username);

    // On Ok the account exists and `out` is an unlocked session with a saved, empty vault.
    static RegisterResult registerUser(const std::string &username, const std::string &password,
                                       std::unique_ptr<Session> &out, const ProgressFn &progress = {});
    // "Unknown user" and "wrong password" both return InvalidCredentials after one key derivation.
    static LoginResult login(const std::string &username, const std::string &password,
                             std::unique_ptr<Session> &out, const ProgressFn &progress = {},
                             LoginPurpose purpose = LoginPurpose::SignIn);
    static bool verifyMasterPassword(const Session &session, const std::string &password, const ProgressFn &progress = {});
    // Verifies `current`, applies the master-password policy to `newPassword`, re-encrypts the vault and
    // rewrites the account line. On failure the previous vault and password stay in force.
    static ChangeResult changeMasterPassword(Session &session, const std::string &current,
                                             const std::string &newPassword, const ProgressFn &progress = {});
};
