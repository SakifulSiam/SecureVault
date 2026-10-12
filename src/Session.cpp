#include "Session.h"
#include "ClipboardManager.h"
#include "Crypto.h"
#include "Utils.h"
#include <stdexcept>

Session::Session(std::string username, std::vector<unsigned char> key, std::vector<unsigned char> s,
                 std::vector<unsigned char> v, std::uint32_t it, int autoLockSeconds)
    : user(std::move(username)), vaultKey(std::move(key)), salt(std::move(s)), verifier(std::move(v)),
      iterations(it), sec(autoLockSeconds)
{
    vaultPtr = std::make_unique<FileVault>(user, vaultKey);
    auditPtr = std::make_unique<AuditLogger>(Utils::auditFile(user).string());
}

Session::~Session()
{
    if (!isLocked())
        lock("LOGOUT", "Session closed");
}

FileVault &Session::vault()
{
    if (!vaultPtr)
        throw std::logic_error("vault is locked");
    return *vaultPtr;
}

AuditLogger &Session::audit()
{
    if (!auditPtr)
        throw std::logic_error("vault is locked");
    return *auditPtr;
}

void Session::lock(const std::string &auditCode, const std::string &detail)
{
    if (!vaultPtr)
        return;
    if (vaultPtr->isReady())
        vaultPtr->save();
    if (auditPtr)
        auditPtr->log(auditCode, detail);
    vaultPtr.reset(); // FileVault destructor wipes its copies
    auditPtr.reset();
    Crypto::wipe(vaultKey);
    Crypto::wipe(verifier);
    ClipboardManager::clear();
}
