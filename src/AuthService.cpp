#include "AuthService.h"
#include "Crypto.h"
#include "PasswordChecker.h"
#include "Utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>

namespace fs = std::filesystem;

namespace
{
    struct UserRecord
    {
        std::vector<unsigned char> salt;
        std::vector<unsigned char> verifier;
        std::uint32_t iterations = 0;
    };

    constexpr std::uint32_t kMaxIterations = 20000000; // refuse absurd values from a damaged users.db

    std::vector<unsigned char> derive(const std::string &password, std::vector<unsigned char> salt, const char *domain, std::uint32_t it)
    {
        for (const char *c = domain; *c; ++c)
            salt.push_back((unsigned char)*c);
        return Crypto::pbkdf2Sha256(password, salt, it, 32);
    }

    void report(const ProgressFn &p, int done, int total)
    {
        if (p)
            p(done, total);
    }

    std::optional<UserRecord> findUser(const std::string &name)
    {
        std::ifstream in(Utils::usersFile());
        std::string line;
        while (std::getline(in, line))
        {
            auto p = Utils::splitEscaped(line);
            if (p.size() < 4 || p[0] != name)
                continue;
            try
            {
                UserRecord r;
                r.salt = Crypto::hexDecode(p[1]);
                r.verifier = Crypto::hexDecode(p[2]);
                unsigned long it = std::stoul(p[3]);
                if (it == 0 || it > kMaxIterations)
                    return std::nullopt;
                r.iterations = (std::uint32_t)it;
                return r;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    bool rewriteUserRecord(const std::string &username, const std::vector<unsigned char> &salt,
                           const std::vector<unsigned char> &authKey, std::uint32_t iterations)
    {
        const auto usersPath = Utils::usersFile();
        const auto tempPath = usersPath.string() + ".tmp";
        const auto rollbackPath = usersPath.string() + ".rollback";

        std::vector<std::string> lines;
        {
            std::ifstream in(usersPath);
            if (!in)
                return false;
            std::string line;
            bool found = false;
            while (std::getline(in, line))
            {
                auto p = Utils::splitEscaped(line);
                if (!p.empty() && p[0] == username)
                {
                    lines.push_back(Utils::joinEscaped({username, Crypto::hexEncode(salt),
                                                        Crypto::hexEncode(Crypto::sha256(authKey)), std::to_string(iterations)}));
                    found = true;
                }
                else
                    lines.push_back(line);
            }
            if (!found)
                return false;
        }
        {
            std::ofstream out(tempPath, std::ios::trunc);
            if (!out)
                return false;
            for (const auto &l : lines)
                out << l << '\n';
            out.flush();
            if (!out)
            {
                std::error_code ec;
                fs::remove(tempPath, ec);
                return false;
            }
        }
        std::error_code ec;
        if (fs::exists(usersPath))
        {
            fs::copy_file(usersPath, rollbackPath, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                fs::remove(tempPath, ec);
                return false;
            }
        }
#ifdef _WIN32
        fs::remove(usersPath, ec);
        if (ec)
        {
            fs::remove(tempPath, ec);
            fs::remove(rollbackPath, ec);
            return false;
        }
#endif
        fs::rename(tempPath, usersPath, ec);
        if (ec)
        {
            if (fs::exists(rollbackPath))
                fs::copy_file(rollbackPath, usersPath, fs::copy_options::overwrite_existing, ec);
            fs::remove(tempPath, ec);
            fs::remove(rollbackPath, ec);
            return false;
        }
        fs::remove(rollbackPath, ec);
        return true;
    }
}

bool AuthService::isValidUsername(const std::string &name)
{
    if (name.size() < 3 || name.size() > 32)
        return false;
    for (unsigned char c : name)
        if (!(std::isalnum(c) || c == '_' || c == '-'))
            return false;
    return true;
}

bool AuthService::anyUserExists()
{
    std::ifstream in(Utils::usersFile());
    std::string line;
    while (std::getline(in, line))
        if (Utils::splitEscaped(line).size() >= 4)
            return true;
    return false;
}

bool AuthService::userExists(const std::string &name) { return findUser(name).has_value(); }

std::uint32_t AuthService::kdfIterations(const std::string &username)
{
    auto r = findUser(username);
    return r ? r->iterations : 0;
}

RegisterResult AuthService::registerUser(const std::string &username, const std::string &password,
                                         std::unique_ptr<Session> &out, const ProgressFn &progress)
{
    out.reset();
    if (!isValidUsername(username))
        return RegisterResult::InvalidUsername;
    {
        std::ifstream in(Utils::usersFile());
        std::string line;
        while (std::getline(in, line))
        {
            auto p = Utils::splitEscaped(line);
            if (!p.empty() && p[0] == username)
                return RegisterResult::UsernameTaken;
        }
    }
    if (!MasterPasswordPolicy::evaluate(password).ok)
        return RegisterResult::WeakPassword;

    const std::uint32_t it = kNewAccountIterations;
    auto salt = Crypto::randomBytes(16);
    report(progress, 0, 2);
    auto authKey = derive(password, salt, "AUTH", it);
    auto verifier = Crypto::sha256(authKey);
    Crypto::wipe(authKey);
    report(progress, 1, 2);
    auto vaultKey = derive(password, salt, "VAULT", it);
    report(progress, 2, 2);

    const bool vaultExisted = fs::exists(Utils::vaultFile(username));
    auto session = std::make_unique<Session>(username, vaultKey, salt, verifier, it);
    Crypto::wipe(vaultKey);
    if (!session->vault().load() || !session->vault().save())
        return RegisterResult::StorageError; // a stray vault file for this name stays untouched

    const auto usersPath = Utils::usersFile();
    bool needNewline = false;
    {
        std::ifstream in(usersPath, std::ios::binary | std::ios::ate);
        if (in && in.tellg() > 0)
        {
            in.seekg(-1, std::ios::end);
            char last = 0;
            in.get(last);
            needNewline = last != '\n';
        }
    }
    std::ofstream usersOut(usersPath, std::ios::app);
    if (!usersOut)
    {
        session->lock("LOGOUT", "Registration failed");
        if (!vaultExisted)
        {
            std::error_code ec;
            fs::remove(Utils::vaultFile(username), ec);
        }
        return RegisterResult::StorageError;
    }
    if (needNewline)
        usersOut << '\n';
    usersOut << Utils::joinEscaped({username, Crypto::hexEncode(salt), Crypto::hexEncode(verifier), std::to_string(it)}) << "\n";
    usersOut.flush();
    if (!usersOut)
        return RegisterResult::StorageError;
    usersOut.close();
    session->audit().log("LOGIN_SUCCESS", "Vault unlocked");
    out = std::move(session);
    return RegisterResult::Ok;
}

LoginResult AuthService::login(const std::string &username, const std::string &password,
                               std::unique_ptr<Session> &out, const ProgressFn &progress, LoginPurpose purpose)
{
    out.reset();
    std::optional<UserRecord> rec;
    if (isValidUsername(username))
        rec = findUser(username);

    // Unknown names pay the same key-derivation cost as a wrong password, so timing does not reveal them.
    static const std::vector<unsigned char> dummySalt(16, 0x5A);
    const std::uint32_t it = rec ? rec->iterations : kNewAccountIterations;
    report(progress, 0, 2);
    auto authKey = derive(password, rec ? rec->salt : dummySalt, "AUTH", it);
    report(progress, 1, 2);
    const bool match = rec && Crypto::constantTimeEqual(Crypto::sha256(authKey), rec->verifier);
    Crypto::wipe(authKey);
    if (!match)
    {
        if (rec) // LOGIN_FAILED is written only for accounts that exist
            AuditLogger(Utils::auditFile(username).string()).log("LOGIN_FAILED", "Wrong master password");
        return LoginResult::InvalidCredentials;
    }
    auto vaultKey = derive(password, rec->salt, "VAULT", it);
    report(progress, 2, 2);
    auto session = std::make_unique<Session>(username, vaultKey, rec->salt, rec->verifier, it);
    Crypto::wipe(vaultKey);
    if (!session->vault().load())
    {
        out = std::move(session);
        return LoginResult::VaultUnreadable;
    }
    if (purpose == LoginPurpose::SignIn)
        session->audit().log("LOGIN_SUCCESS", "Vault unlocked");
    else
        session->audit().log("VAULT_UNLOCK", "Unlocked with master password");
    out = std::move(session);
    return LoginResult::Ok;
}

bool AuthService::verifyMasterPassword(const Session &session, const std::string &password, const ProgressFn &progress)
{
    if (session.isLocked())
        return false;
    report(progress, 0, 1);
    auto authKey = derive(password, session.salt, "AUTH", session.iterations);
    report(progress, 1, 1);
    const bool ok = Crypto::constantTimeEqual(Crypto::sha256(authKey), session.verifier);
    Crypto::wipe(authKey);
    return ok;
}

ChangeResult AuthService::changeMasterPassword(Session &session, const std::string &current,
                                               const std::string &newPassword, const ProgressFn &progress)
{
    if (session.isLocked() || !session.vault().isReady())
        return ChangeResult::VaultNotReady;
    report(progress, 0, 3);
    if (!verifyMasterPassword(session, current))
        return ChangeResult::WrongPassword;
    if (!MasterPasswordPolicy::evaluate(newPassword).ok)
        return ChangeResult::WeakPassword;
    report(progress, 1, 3);

    const std::uint32_t it = kNewAccountIterations;
    auto salt = Crypto::randomBytes(16);
    auto newAuth = derive(newPassword, salt, "AUTH", it);
    report(progress, 2, 3);
    auto newVault = derive(newPassword, salt, "VAULT", it);
    report(progress, 3, 3);

    FileVault &vault = session.vault();
    const std::string vaultPath = Utils::vaultFile(session.user).string();
    const std::string rollback = vaultPath + ".rollback";
    std::error_code ec;
    fs::copy_file(vaultPath, rollback, fs::copy_options::overwrite_existing, ec);
    const bool haveRollback = !ec;

    std::vector<unsigned char> oldKey = session.vaultKey;
    const bool vaultChanged = vault.rekey(newVault);
    const bool userChanged = vaultChanged && rewriteUserRecord(session.user, salt, newAuth, it);
    ChangeResult result;
    if (vaultChanged && userChanged)
    {
        std::vector<unsigned char> newVerifier = Crypto::sha256(newAuth);
        Crypto::wipe(session.vaultKey);
        session.vaultKey = newVault;
        session.salt = salt;
        session.verifier = newVerifier;
        session.iterations = it;
        if (haveRollback)
            fs::remove(rollback, ec);
        session.audit().log("CHANGE_MASTER_PASSWORD");
        result = ChangeResult::Ok;
    }
    else
    {
        if (vaultChanged && !vault.rekey(oldKey))
        {
            // Could not re-encrypt back; put the old file back and close the session so the caller reloads.
            if (haveRollback)
                fs::copy_file(rollback, vaultPath, fs::copy_options::overwrite_existing, ec);
            Crypto::wipe(oldKey);
            Crypto::wipe(newAuth);
            Crypto::wipe(newVault);
            session.lock("LOCK_VAULT", "Master password change failed");
            return ChangeResult::StorageError;
        }
        if (haveRollback)
            fs::remove(rollback, ec);
        result = ChangeResult::StorageError;
    }
    Crypto::wipe(oldKey);
    Crypto::wipe(newAuth);
    Crypto::wipe(newVault);
    return result;
}
