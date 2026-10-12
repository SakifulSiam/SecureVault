// SecureVault CLI: a thin front-end over securevault_core. All business logic lives in the core.
#include "AuthService.h"
#include "ClipboardManager.h"
#include "Console.h"
#include "Crypto.h"
#include "InstanceLock.h"
#include "PasswordChecker.h"
#include "PasswordGenerator.h"
#include "Session.h"
#include "Utils.h"
#include "VaultAnalyzer.h"
#include "Version.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

using Console::inputInt;
using Console::inputLine;
using Console::yesNo;

static std::atomic<bool> timeoutFlag{false};
static std::atomic<bool> stopTimer{false};

static void timerLoop(SecurityManager *sec)
{
    while (!stopTimer)
    {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (sec->timedOut())
            timeoutFlag = true;
    }
}
static std::string mask(const std::string &s) { return std::string(s.size(), '*'); }
static std::string maskedCard(const std::string &s)
{
    if (s.size() <= 4)
        return mask(s);
    return std::string(s.size() - 4, '*') + s.substr(s.size() - 4);
}
static void printPassword(const PasswordEntry &e, bool reveal = false)
{
    std::cout << "[" << e.id << "] " << e.title << " | " << e.site << " | " << e.category << (e.favorite ? " | ★" : "") << "\n";
    std::cout << "    Username : " << e.username << "\n";
    std::cout << "    Password : " << (reveal ? e.password : mask(e.password)) << "\n";
    std::cout << "    Updated  : " << Utils::timeString(e.updatedAt) << "\n";
}
static void printNote(const SecureNote &n) { std::cout << "[" << n.id << "] " << n.title << " | " << n.category << (n.favorite ? " | ★" : "") << "\n"; }
static void printCard(const CardItem &c, bool reveal = false)
{
    std::cout << "[" << c.id << "] " << c.title << (c.favorite ? " | ★" : "") << "\n  Cardholder: " << c.cardholder << "\n  Number: "
              << (reveal ? c.number : maskedCard(c.number)) << "\n  Expiry: " << c.expiry << "\n  CVV: " << (reveal ? c.cvv : mask(c.cvv)) << "\n";
}

static void showHealth(const FileVault &vault)
{
    HealthReport r = VaultAnalyzer::analyze(vault);
    std::cout << "\n=== SECURITY CENTER ===\nTotal credentials : " << r.total << "\nStrong passwords  : " << r.strong
              << "\nWeak passwords    : " << r.weak << "\nReused passwords  : " << r.reused << "\nOld passwords     : " << r.old
              << "\nSecurity score    : " << r.score << "/100\n";
    auto title = [&](int id)
    {
        for (const auto &e : vault.getPasswords())
            if (e.id == id)
                return e.title;
        return std::string();
    };
    if (r.weak)
    {
        std::cout << "\nWeak credentials:\n";
        for (const auto &f : r.credentials)
            if (f.weak)
                std::cout << " - [" << f.id << "] " << title(f.id) << "\n";
    }
    if (r.old)
    {
        std::cout << "\nOlder than 180 days:\n";
        for (const auto &f : r.credentials)
            if (f.old)
                std::cout << " - [" << f.id << "] " << title(f.id) << "\n";
    }
}

static void listPasswords(const FileVault &v)
{
    if (v.getPasswords().empty())
    {
        std::cout << "No credentials stored.\n";
        return;
    }
    for (const auto &e : v.getPasswords())
        printPassword(e);
}
static void addPassword(FileVault &v, PasswordGenerator &g, AuditLogger &log)
{
    PasswordEntry e;
    e.title = inputLine("Title: ");
    e.site = inputLine("Website/service: ");
    e.username = inputLine("Username: ");
    e.category = inputLine("Category [Other]: ");
    if (e.category.empty())
        e.category = "Other";
    if (yesNo("Generate a password"))
    {
        PasswordGeneratorOptions o;
        o.length = inputInt("Length (8-64): ", 8, 64);
        o.upper = yesNo("Include uppercase");
        o.lower = yesNo("Include lowercase");
        o.digits = yesNo("Include digits");
        o.symbols = yesNo("Include symbols");
        e.password = g.generate(o);
        std::cout << "Generated: " << e.password << "\n";
    }
    else
        e.password = inputLine("Password: ");
    e.notes = inputLine("Notes (optional): ");
    e.favorite = yesNo("Favorite");
    e.createdAt = e.updatedAt = e.passwordChangedAt = std::time(nullptr);
    if (v.addPassword(e))
    {
        log.log("ADD_ENTRY", e.title);
        std::cout << "Credential added.\n";
    }
    else
        std::cout << "Could not add credential (same website and username already stored, or storage error).\n";
    Crypto::wipe(e.password);
}
static void viewPassword(FileVault &v, AuditLogger &log)
{
    int id = inputInt("Credential ID: ", 1, 1000000000);
    auto p = v.findPassword(id);
    if (!p)
    {
        std::cout << "Not found.\n";
        return;
    }
    printPassword(*p);
    int c = inputInt("1. Reveal  2. Copy username  3. Copy password  4. Password history  5. Back\nChoice: ", 1, 5);
    if (c == 1)
    {
        log.log("REVEAL_PASSWORD", p->title);
        printPassword(*p, true);
    }
    else if (c == 2)
    {
        std::cout << "Clipboard: " << (ClipboardManager::copy(p->username) ? "copied" : "unavailable") << "\n";
    }
    else if (c == 3)
    {
        bool copied = ClipboardManager::copyWithAutoClear(p->password, 30);
        std::cout << "Clipboard: " << (copied ? "copied" : "unavailable") << "\n";
        if (copied)
        {
            log.log("COPY_PASSWORD", p->title);
            std::cout << "Remember to clear the clipboard after use.\n";
        }
    }
    else if (c == 4)
    {
        if (p->history.empty())
            std::cout << "No previous passwords.\n";
        else
            for (auto &h : p->history)
                std::cout << Utils::timeString(h.changedAt) << " | " << mask(h.password) << "\n";
    }
}
static void editPassword(FileVault &v, AuditLogger &log)
{
    int id = inputInt("Credential ID: ", 1, 1000000000);
    auto p = v.findPassword(id);
    if (!p)
    {
        std::cout << "Not found.\n";
        return;
    }
    PasswordEntry e = *p;
    e.title = inputLine("New title (leave blank to keep): ");
    if (e.title.empty())
        e.title = p->title;
    e.site = inputLine("New website (leave blank to keep): ");
    if (e.site.empty())
        e.site = p->site;
    e.username = inputLine("New username (leave blank to keep): ");
    if (e.username.empty())
        e.username = p->username;
    if (yesNo("Change password"))
        e.password = inputLine("New password: ");
    e.category = inputLine("New category (leave blank to keep): ");
    if (e.category.empty())
        e.category = p->category;
    e.notes = inputLine("New notes (leave blank to keep): ");
    if (e.notes.empty())
        e.notes = p->notes;
    e.favorite = yesNo("Favorite");
    if (v.updatePassword(e))
    {
        log.log("UPDATE_ENTRY", e.title);
        std::cout << "Updated.\n";
    }
    else
        std::cout << "Update failed.\n";
    Crypto::wipe(e.password);
}
static void deletePassword(FileVault &v, AuditLogger &log)
{
    int id = inputInt("Credential ID: ", 1, 1000000000);
    auto p = v.findPassword(id);
    if (!p)
    {
        std::cout << "Not found.\n";
        return;
    }
    std::string title = p->title;
    if (yesNo("Delete " + title + " permanently?"))
    {
        if (v.deletePassword(id))
        {
            log.log("DELETE_ENTRY", title);
            std::cout << "Deleted.\n";
        }
    }
}
static void notesMenu(FileVault &v, AuditLogger &log)
{
    int c = inputInt("\n1.Add note 2.List notes 3.View note 4.Delete note 5.Back\nChoice: ", 1, 5);
    if (c == 1)
    {
        SecureNote n;
        n.title = inputLine("Title: ");
        n.content = inputLine("Content: ");
        n.category = inputLine("Category [Notes]: ");
        if (n.category.empty())
            n.category = "Notes";
        n.favorite = yesNo("Favorite");
        n.createdAt = n.updatedAt = std::time(nullptr);
        if (v.addNote(n))
        {
            log.log("ADD_NOTE", n.title);
            std::cout << "Note saved.\n";
        }
    }
    else if (c == 2)
    {
        for (auto &n : v.getNotes())
            printNote(n);
        if (v.getNotes().empty())
            std::cout << "No notes.\n";
    }
    else if (c == 3)
    {
        int id = inputInt("Note ID: ", 1, 1000000000);
        auto n = v.findNote(id);
        if (n)
            std::cout << "\n" << n->title << "\n" << n->content << "\n";
        else
            std::cout << "Not found.\n";
    }
    else if (c == 4)
    {
        int id = inputInt("Note ID: ", 1, 1000000000);
        auto n = v.findNote(id);
        std::string title = n ? n->title : std::to_string(id);
        if (v.deleteNote(id))
            log.log("DELETE_NOTE", title);
    }
}
static void cardsMenu(FileVault &v, AuditLogger &log)
{
    int c = inputInt("\n1.Add card 2.List cards 3.View card 4.Delete card 5.Back\nChoice: ", 1, 5);
    if (c == 1)
    {
        CardItem x;
        x.title = inputLine("Title: ");
        x.cardholder = inputLine("Cardholder: ");
        x.number = inputLine("Number: ");
        x.expiry = inputLine("Expiry: ");
        x.cvv = inputLine("CVV: ");
        x.notes = inputLine("Notes: ");
        x.favorite = yesNo("Favorite");
        x.createdAt = x.updatedAt = std::time(nullptr);
        if (v.addCard(x))
        {
            log.log("ADD_CARD", x.title);
            std::cout << "Card saved.\n";
        }
    }
    else if (c == 2)
    {
        for (auto &x : v.getCards())
            printCard(x);
        if (v.getCards().empty())
            std::cout << "No cards.\n";
    }
    else if (c == 3)
    {
        int id = inputInt("Card ID: ", 1, 1000000000);
        auto x = v.findCard(id);
        if (x)
        {
            printCard(*x);
            if (yesNo("Reveal sensitive fields"))
            {
                log.log("REVEAL_CARD", x->title);
                printCard(*x, true);
            }
        }
        else
            std::cout << "Not found.\n";
    }
    else if (c == 4)
    {
        int id = inputInt("Card ID: ", 1, 1000000000);
        auto x = v.findCard(id);
        std::string title = x ? x->title : std::to_string(id);
        if (v.deleteCard(id))
            log.log("DELETE_CARD", title);
    }
}

static void printRecentActivity(const AuditLogger &audit, size_t count = 15)
{
    std::cout << "\n--- Recent Security Activity ---\n";
    const auto &ev = audit.getEvents();
    size_t start = ev.size() > count ? ev.size() - count : 0;
    for (size_t i = start; i < ev.size(); ++i)
        std::cout << Utils::timeString(ev[i].timestamp) << " | " << ev[i].action << (ev[i].detail.empty() ? "" : " | " + ev[i].detail) << "\n";
    if (ev.empty())
        std::cout << "No events recorded.\n";
}

static void printRegisterResult(RegisterResult r, const std::string &username)
{
    switch (r)
    {
    case RegisterResult::Ok:
        std::cout << "User registered successfully.\n";
        break;
    case RegisterResult::InvalidUsername:
        if (username.size() < 3 || username.size() > 32)
            std::cout << "Username must be 3-32 characters.\n";
        else
            std::cout << "Username may contain only letters, digits, '_' and '-'.\n";
        break;
    case RegisterResult::UsernameTaken:
        std::cout << "Username already exists.\n";
        break;
    case RegisterResult::WeakPassword:
        std::cout << "Master password is too weak. Use at least 12 characters and improve its strength.\n";
        break;
    case RegisterResult::StorageError:
        std::cout << "Could not create the account (storage error).\n";
        break;
    }
}

// Prompts for username and password once. Returns true on success.
static bool promptLogin(std::unique_ptr<Session> &out, LoginPurpose purpose)
{
    std::string u = inputLine("Username: ");
    std::string pw = Console::inputPassword();
    LoginResult r = AuthService::login(u, pw, out, {}, purpose);
    Crypto::wipe(pw);
    if (r == LoginResult::Ok)
        return true;
    if (r == LoginResult::VaultUnreadable)
    {
        std::cout << "Vault could not be opened. It may be corrupted or incompatible.\n";
        out.reset();
        return false;
    }
    std::cout << "Invalid credentials.\n";
    return false;
}

int main(int argc, char **argv)
{
    try
    {
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--data-dir" && i + 1 < argc)
                Utils::setDataDir(argv[++i]);
        if (auto legacy = Utils::legacyWorkingDirData())
            if (yesNo("Found an older vault folder at " + legacy->string() + ". Use it instead of " + Utils::dataDir().string() + "?"))
                Utils::setDataDir(*legacy);

        InstanceLock instanceLock;
        if (!instanceLock.tryAcquire())
        {
            std::cout << "SecureVault is already open for this data folder.\n";
            return 1;
        }

        std::cout << "=== SecureVault CLI " << SV_VERSION << " ===\nLocal encrypted password manager\n\n1. Register\n2. Login\n3. Exit\n";
        int first = inputInt("Choice: ", 1, 3);
        if (first == 3)
            return 0;
        std::unique_ptr<Session> session;
        bool ok = false;
        if (first == 1)
        {
            std::string username = inputLine("Username: ");
            std::string pw = Console::inputPassword();
            RegisterResult r = AuthService::registerUser(username, pw, session);
            Crypto::wipe(pw);
            printRegisterResult(r, username);
            ok = r == RegisterResult::Ok;
        }
        else
        {
            for (int i = 1; i <= 5 && !ok; i++)
            {
                ok = promptLogin(session, LoginPurpose::SignIn);
                if (!ok)
                {
                    std::cout << "Attempts remaining: " << 5 - i << "\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(900));
                }
            }
        }
        if (!ok)
            return 0;
        if (session->vault().skippedRecords() > 0)
            std::cout << session->vault().skippedRecords() << " item(s) couldn't be read. They are kept in the vault file untouched.\n";

        PasswordGenerator generator;
        PasswordChecker checker;
        SecurityManager activity; // idle timer for this terminal session
        SecurityManager gate;     // failed re-authentication cooldown
        std::atomic<bool> running{true};
        std::thread timer(timerLoop, &activity);

        auto relogin = [&](const char *)
        {
            while (true)
            {
                std::unique_ptr<Session> next;
                if (promptLogin(next, LoginPurpose::Unlock))
                {
                    session = std::move(next);
                    gate.resetFailures();
                    activity.touch();
                    return;
                }
                std::cout << "Authentication failed.\n";
                gate.recordFailure(1000000);
                if (int wait = gate.lockoutRemaining())
                {
                    std::cout << "Wait " << wait << " seconds before trying again.\n";
                    std::this_thread::sleep_for(std::chrono::seconds(wait));
                }
            }
        };

        while (running)
        {
            FileVault &vault = session->vault();
            AuditLogger &audit = session->audit();
            if (timeoutFlag)
            {
                timeoutFlag = false;
                std::cout << "\n[SECURITY] Vault auto-locked after inactivity.\n";
                session->lock("AUTO_LOCK", "Inactivity timeout");
                relogin("auto");
                continue;
            }
            activity.touch();
            std::cout << "\n===== SECUREVAULT =====\n1. Add credential\n2. List credentials\n3. View credential\n4. Edit credential\n5. Delete credential\n6. Search\n7. Favorites\n8. Generate password\n9. Password strength\n10. Security Center\n11. Secure Notes\n12. Cards\n13. Audit log\n14. Backup vault\n15. Restore vault\n16. Change master password\n17. Lock vault\n18. Exit\n";
            int c = inputInt("Choice: ", 1, 18);
            activity.touch();
            try
            {
                if (c == 1)
                    addPassword(vault, generator, audit);
                else if (c == 2)
                    listPasswords(vault);
                else if (c == 3)
                    viewPassword(vault, audit);
                else if (c == 4)
                    editPassword(vault, audit);
                else if (c == 5)
                    deletePassword(vault, audit);
                else if (c == 6)
                {
                    auto q = inputLine("Search: ");
                    auto ids = vault.searchPasswordIds(q);
                    for (int id : ids)
                        if (auto *e = vault.findPassword(id))
                            printPassword(*e);
                    if (ids.empty())
                        std::cout << "No matches.\n";
                }
                else if (c == 7)
                {
                    auto ids = vault.favoritePasswordIds();
                    for (int id : ids)
                        if (auto *e = vault.findPassword(id))
                            printPassword(*e);
                    if (ids.empty())
                        std::cout << "No favorites.\n";
                }
                else if (c == 8)
                {
                    PasswordGeneratorOptions o;
                    o.length = inputInt("Length (8-64): ", 8, 64);
                    o.upper = yesNo("Uppercase");
                    o.lower = yesNo("Lowercase");
                    o.digits = yesNo("Digits");
                    o.symbols = yesNo("Symbols");
                    o.avoidAmbiguous = yesNo("Avoid ambiguous characters");
                    auto p = generator.generate(o);
                    std::cout << "Generated: " << p << "\n";
                    if (yesNo("Copy to clipboard"))
                        std::cout << (ClipboardManager::copyWithAutoClear(p, 30) ? "Copied.\n" : "Clipboard unavailable.\n");
                    Crypto::wipe(p);
                }
                else if (c == 9)
                {
                    auto p = inputLine("Password: ");
                    auto a = checker.assess(p);
                    std::cout << "Strength: " << a.displayLabel() << " | Score: " << a.score << "/100\n"
                              << (a.commonPattern ? "Warning: common/repeated pattern detected.\n" : "");
                    Crypto::wipe(p);
                }
                else if (c == 10)
                    showHealth(vault);
                else if (c == 11)
                    notesMenu(vault, audit);
                else if (c == 12)
                    cardsMenu(vault, audit);
                else if (c == 13)
                    printRecentActivity(audit);
                else if (c == 14)
                {
                    std::string dest = inputLine("Backup file path: ");
                    if (dest.empty())
                        dest = (Utils::dataDir() / "backups" / (session->username() + "_backup.sv")).string();
                    bool done = vault.backup(dest);
                    std::cout << (done ? "Encrypted backup created.\n" : "Backup failed.\n");
                    if (done)
                        audit.log("BACKUP", dest);
                }
                else if (c == 15)
                {
                    std::string src = inputLine("Encrypted backup path: ");
                    if (vault.restore(src))
                    {
                        audit.log("RESTORE", src);
                        std::cout << "Backup restored.\n";
                    }
                    else
                        std::cout << "Restore failed.\n";
                }
                else if (c == 16)
                {
                    std::string current = Console::inputPassword("Current master password: ");
                    std::string np = Console::inputPassword("New master password: ");
                    std::string confirm = Console::inputPassword("Confirm new master password: ");
                    if (np != confirm)
                        std::cout << "Passwords do not match.\n";
                    else
                    {
                        switch (AuthService::changeMasterPassword(*session, current, np))
                        {
                        case ChangeResult::Ok:
                            std::cout << "Master password changed and vault re-encrypted.\n";
                            break;
                        case ChangeResult::WrongPassword:
                            std::cout << "Current master password is incorrect.\n";
                            break;
                        case ChangeResult::WeakPassword:
                            std::cout << "New master password is too weak.\n";
                            break;
                        default:
                            std::cout << "Master password change failed; the original vault and master password were restored.\n";
                            if (session->isLocked())
                                relogin("change");
                            break;
                        }
                    }
                    Crypto::wipe(current);
                    Crypto::wipe(np);
                    Crypto::wipe(confirm);
                }
                else if (c == 17)
                {
                    session->lock("LOCK_VAULT", "Manual lock");
                    relogin("manual");
                }
                else if (c == 18)
                {
                    session->lock("LOGOUT", "");
                    running = false;
                }
            }
            catch (const std::exception &ex)
            {
                std::cout << "Operation failed: " << ex.what() << "\n";
            }
        }
        stopTimer = true;
        timer.join();
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cout << "\n" << ex.what() << "\n";
        return 1;
    }
}
