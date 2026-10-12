// Additional core tests for Phase 0 (framework-free). Complements backend_regression_tests.cpp.
#include "AuthService.h"
#include "ClipboardManager.h"
#include "Crypto.h"
#include "FileVault.h"
#include "InstanceLock.h"
#include "PasswordChecker.h"
#include "PasswordGenerator.h"
#include "SecurityManager.h"
#include "Session.h"
#include "Utils.h"
#include "VaultAnalyzer.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
namespace fs = std::filesystem;

static int failed = 0, total = 0;
#define CHECK(label, cond)                                             \
    do                                                                 \
    {                                                                  \
        ++total;                                                       \
        bool ok_ = (cond);                                             \
        std::cout << (ok_ ? "[ok]    " : "[FAIL]  ") << label << "\n"; \
        if (!ok_)                                                      \
            ++failed;                                                  \
    } while (0)

static std::vector<unsigned char> key(unsigned char b) { return std::vector<unsigned char>(32, b); }
static std::string slurp(const std::string &p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}
static PasswordEntry entry(const std::string &t, const std::string &s, const std::string &u, const std::string &p)
{
    PasswordEntry e;
    e.title = t;
    e.site = s;
    e.username = u;
    e.password = p;
    e.createdAt = e.updatedAt = std::time(nullptr);
    return e;
}
static void fresh()
{
    fs::current_path(fs::temp_directory_path());
    fs::remove_all("sv_core");
    fs::create_directories("sv_core");
    fs::current_path("sv_core");
    Utils::setDataDir(fs::current_path() / "data");
}

// The ORIGINAL escaping, kept here to prove byte-level compatibility in both directions.
static std::string oldJoin(const std::vector<std::string> &f)
{
    std::string out;
    for (size_t i = 0; i < f.size(); ++i)
    {
        if (i)
            out += '|';
        for (char c : f[i])
        {
            if (c == '\\' || c == '|')
                out += '\\';
            out += c;
        }
    }
    return out;
}
static std::vector<std::string> oldSplit(const std::string &line)
{
    std::vector<std::string> out(1);
    bool esc = false;
    for (char c : line)
    {
        if (esc)
        {
            out.back() += c;
            esc = false;
        }
        else if (c == '\\')
            esc = true;
        else if (c == '|')
            out.emplace_back();
        else
            out.back() += c;
    }
    if (esc)
        out.back() += '\\';
    return out;
}

int main()
{
    // ---- Escaping: legacy compatible in both directions, new line-break escapes round-trip.
    {
        std::mt19937 rng(42);
        const std::string alpha = "ab\\|nr x1";
        bool fwd = true, back = true, lines = true;
        for (int i = 0; i < 20000; ++i)
        {
            std::vector<std::string> f(1 + rng() % 4);
            for (auto &s : f)
            {
                size_t n = rng() % 8;
                for (size_t k = 0; k < n; ++k)
                    s += alpha[rng() % alpha.size()];
            }
            fwd = fwd && Utils::splitEscaped(oldJoin(f)) == f;   // old writer -> new reader
            back = back && oldSplit(Utils::joinEscaped(f)) == f; // new writer -> old reader (no line breaks present)
            std::vector<std::string> g = f;
            g[0] += "\n\r\\n"; // new writer -> new reader with line breaks
            lines = lines && Utils::splitEscaped(Utils::joinEscaped(g)) == g && Utils::joinEscaped(g).find('\n') == std::string::npos;
        }
        CHECK("escaping: old writer -> new reader (20000 random records)", fwd);
        CHECK("escaping: new writer -> old reader when no line breaks (20000 random records)", back);
        CHECK("escaping: LF/CR round-trip and never leave a raw newline in a record", lines);
    }

    // ---- Failed save: transactional mutators leave memory and file unchanged.
    fresh();
    {
        FileVault v("t", key(1));
        v.load();
        v.addPassword(entry("A", "a.com", "u", "Old!Pass12345aa"));
        v.addNote([]
                  { SecureNote n; n.title = "N"; n.content = "c"; return n; }());
        const std::string before = slurp("data/t_vault.sv");
        fs::create_directory("data/t_vault.sv.tmp"); // a directory in the way makes the temp file unwritable
        CHECK("failed save: addPassword returns false", !v.addPassword(entry("B", "b.com", "u", "Zq!9xWvLk2#Rt")));
        CHECK("failed save: no phantom credential in memory", v.getPasswords().size() == 1);
        auto e = v.getPasswords()[0];
        e.title = "Changed";
        e.password = "New!Pass12345bb";
        CHECK("failed save: updatePassword returns false", !v.updatePassword(e));
        const auto &kept = v.getPasswords()[0];
        CHECK("failed save: update rolled back (title, password, history, age)", kept.title == "A" && kept.password == "Old!Pass12345aa" && kept.history.empty());
        CHECK("failed save: deleteNote returns false and the note is still there", !v.deleteNote(v.getNotes()[0].id) && v.getNotes().size() == 1);
        CHECK("failed save: deletePassword restores the item", !v.deletePassword(v.getPasswords()[0].id) && v.getPasswords().size() == 1);
        CHECK("failed save: rekey fails and keeps the old key", !v.rekey(key(5)) && FileVault("t", key(1)).load());
        CHECK("failed save: vault file byte-identical", slurp("data/t_vault.sv") == before);
        fs::remove("data/t_vault.sv.tmp");
        CHECK("after the fault clears, writes work again with the old key", v.addPassword(entry("C", "c.com", "u", "Zq!9xWvLk2#Rt")) && FileVault("t", key(1)).load());
    }

    // ---- Malformed records are counted, kept, and never silently lost.
    fresh();
    {
        std::string plain = "SV2\n"
                            "P|1|Good|g.com|u|pw|n|Dev|0|1700000000|1700000000|0\n"
                            "P|notanumber|Bad|b.com|u|pw|n|Dev|0|1|1|0\n"
                            "Z|99|future|record\n"
                            "N|2|Note|content|Notes|0|1700000000|1700000000\n";
        fs::create_directories("data");
        std::ofstream("data/t_vault.sv") << Crypto::encryptAesGcm(plain, key(1));
        FileVault v("t", key(1));
        CHECK("malformed: load succeeds, readable records kept", v.load() && v.getPasswords().size() == 1 && v.getNotes().size() == 1);
        CHECK("malformed: skippedRecords() counts the two unreadable lines", v.skippedRecords() == 2);
        CHECK("malformed: a legacy vault gets passwordChangedAt = updatedAt", v.getPasswords()[0].passwordChangedAt == 1700000000);
        v.addPassword(entry("New", "n.com", "u", "Zq!9xWvLk2#Rt"));
        FileVault r("t", key(1));
        r.load();
        CHECK("malformed: unreadable lines survive a later save", r.skippedRecords() == 2 && r.getPasswords().size() == 2);
    }

    // ---- passwordChangedAt semantics and history timestamp.
    fresh();
    {
        FileVault v("t", key(1));
        v.load();
        auto e = entry("A", "a.com", "u", "Old!Pass12345aa");
        e.passwordChangedAt = std::time(nullptr) - 400 * 86400;
        v.addPassword(e);
        int id = v.getPasswords()[0].id;
        auto f = *v.findPassword(id);
        f.password = "New!Pass12345bb";
        v.updatePassword(f);
        CHECK("history item carries the time the old password was set", v.findPassword(id)->history.size() == 1 && v.findPassword(id)->history[0].changedAt == e.passwordChangedAt);
        CHECK("changing the password resets passwordChangedAt", std::time(nullptr) - v.findPassword(id)->passwordChangedAt < 5);
        FileVault r("t", key(1));
        r.load();
        CHECK("passwordChangedAt survives save and load", r.getPasswords()[0].passwordChangedAt == v.findPassword(id)->passwordChangedAt);
        CHECK("history is capped at five", [&]
              { for (int i = 0; i < 8; ++i) { auto g = *v.findPassword(id); g.password = "Zq!9xWvLk2#Rt" + std::to_string(i); v.updatePassword(g); } return v.findPassword(id)->history.size() == 5; }());
    }

    // ---- Single-line fields reject line breaks; multi-line fields accept them.
    fresh();
    {
        FileVault v("t", key(1));
        v.load();
        CHECK("title with a line break is rejected", !v.addPassword(entry("A\nB", "a.com", "u", "pw")));
        SecureNote n;
        n.title = "ok";
        n.content = "multi\nline";
        CHECK("note content with line breaks is accepted", v.addNote(n));
        CardItem c;
        c.title = "c";
        c.expiry = "12/30\n";
        CHECK("card expiry with a line break is rejected", !v.addCard(c));
    }

    // ---- Search (ids only), favourites, categories.
    fresh();
    {
        FileVault v("t", key(1));
        v.load();
        auto a = entry("Bank", "bank.com", "alice", "pw1");
        a.category = "Finance";
        a.favorite = true;
        v.addPassword(a);
        SecureNote n;
        n.title = "Wifi";
        n.content = "the SECRET phrase";
        n.category = "Home";
        n.favorite = true;
        v.addNote(n);
        CardItem c;
        c.title = "Visa";
        c.cardholder = "Alice A";
        c.number = "4111111111111111";
        c.cvv = "987";
        c.notes = "travel";
        v.addCard(c);
        CHECK("searchPasswordIds is case-insensitive over title/site/username/category/notes", v.searchPasswordIds("ALICE").size() == 1 && v.searchPasswordIds("finance").size() == 1 && v.searchPasswordIds("pw1").empty());
        CHECK("searchNoteIds searches content", v.searchNoteIds("secret").size() == 1 && v.searchNoteIds("home").size() == 1);
        CHECK("searchCardIds never matches number or CVV", v.searchCardIds("4111").empty() && v.searchCardIds("987").empty() && v.searchCardIds("alice").size() == 1 && v.searchCardIds("travel").size() == 1);
        CHECK("favourites for passwords and notes", v.favoritePasswordIds().size() == 1 && v.favoriteNoteIds().size() == 1 && v.favoriteCardIds().empty());
        CHECK("categories() lists password and note categories", v.categories() == std::vector<std::string>({"Finance", "Home"}));
    }

    // ---- Strength label vs weak flag, master-password policy.
    {
        PasswordChecker pc;
        bool agree = true;
        for (std::string p : {"", "a", "password", "MyPassword#2024xyz", "aaaaaaaaaaaaaaaaaaaa", "abcdefghijklmnopqrst", "Tr0ub4dor&3xKcd!", "correct-horse-battery-staple", "Zq!9xWvLk2#Rt", "12345678", "Qwerty123456!", "X9$kL2#mQ8@vZ4&nB7!w"})
        {
            auto a = pc.assess(p);
            agree = agree && ((a.displayLabel() == "Weak") == a.weak) && (a.weak == !a.weakReason.empty());
        }
        CHECK("displayLabel() is Weak exactly when weak, for every sample", agree);
        CHECK("MyPassword#2024xyz is shown as Weak", pc.assess("MyPassword#2024xyz").displayLabel() == "Weak");
        CHECK("20 lowercase letters are shown as Weak with reason Too simple", pc.assess("abcdefghijklmnopqrst").displayLabel() == "Weak" && pc.assess("abcdefghijklmnopqrst").weakReason == "Too simple");
        CHECK("master policy rejects Password1234!, Qwerty123456!, Summer2024!!!!",
              !MasterPasswordPolicy::evaluate("Password1234!").ok && !MasterPasswordPolicy::evaluate("Qwerty123456!").ok && !MasterPasswordPolicy::evaluate("Summer2024!!!!").ok);
        CHECK("master policy accepts Tr0ub4dor&3xKcd!", MasterPasswordPolicy::evaluate("Tr0ub4dor&3xKcd!").ok);
        CHECK("master policy rejects short passwords", !MasterPasswordPolicy::evaluate("Ab1!xY").lengthOk);
    }

    // ---- Generator extras.
    {
        PasswordGenerator g;
        PasswordGeneratorOptions o;
        CHECK("wordListSize() is 2048", PasswordGenerator::wordListSize() == 2048);
        CHECK("default passphrase has 5 words", [&]
              { auto p = g.generatePassphrase(); int dashes = 0; for (char c : p) dashes += c == '-'; return dashes == 4; }());
        CHECK("passphrase length bounds (3..12)", g.generatePassphrase(2).empty() && g.generatePassphrase(13).empty() && !g.generatePassphrase(3).empty());
        CHECK("poolSize: all classes with ambiguity removed", PasswordGenerator::poolSize(o) > 0);
        o.upper = o.digits = o.symbols = false;
        o.avoidAmbiguous = false;
        CHECK("poolSize: lowercase only is 26; invalid options give 0", PasswordGenerator::poolSize(o) == 26 && PasswordGenerator::poolSize({7, 1, 1, 1, 1, 1}) == 0);
    }

    // ---- VaultAnalyzer.
    fresh();
    {
        FileVault v("t", key(1));
        v.load();
        auto empty = VaultAnalyzer::analyze(v);
        CHECK("analyzer: empty vault scores 100, hasData false", !empty.hasData && empty.score == 100 && empty.band == "Excellent");
        v.addPassword(entry("Weak", "w.com", "u", "abc"));
        auto old = entry("Old", "o.com", "u", "Zq!9xWvLk2#Rt");
        old.passwordChangedAt = std::time(nullptr) - 200 * 86400;
        v.addPassword(old);
        v.addPassword(entry("R1", "r1.com", "u", "Kp#7mWq!2xLzTv9"));
        v.addPassword(entry("R2", "r2.com", "u", "Kp#7mWq!2xLzTv9"));
        v.addPassword(entry("Fine", "f.com", "u", "Hn4$vB8!qZr2Wm6"));
        auto r = VaultAnalyzer::analyze(v);
        CHECK("analyzer: counts", r.total == 5 && r.weak == 1 && r.old == 1 && r.reused == 2 && r.strong == 4);
        CHECK("analyzer: score = 100 - 10*1 - 5*1 - 5*2 = 75, band Good", r.score == 75 && r.band == "Good");
        CHECK("analyzer: attentionCount counts distinct credentials with an issue", r.attentionCount == 4);
        CHECK("analyzer: one reuse group holding ids only", r.reuseGroups.size() == 1 && r.reuseGroups[0].size() == 2);
        CHECK("analyzer: band edges", VaultAnalyzer::bandFor(90) == "Excellent" && VaultAnalyzer::bandFor(89) == "Good" && VaultAnalyzer::bandFor(75) == "Good" && VaultAnalyzer::bandFor(74) == "Needs attention" && VaultAnalyzer::bandFor(60) == "Needs attention" && VaultAnalyzer::bandFor(59) == "Weak");
        auto f = *v.findPassword(r.credentials[0].id);
        f.favorite = true;
        v.updatePassword(f);
        CHECK("analyzer: starring a credential does not change old count", VaultAnalyzer::analyze(v).old == 1);
    }

    // ---- SecurityManager.
    {
        SecurityManager s(1);
        CHECK("security: idleSeconds starts at 0 and timedOut() is false", s.idleSeconds() == 0 && !s.timedOut());
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        CHECK("security: times out after the configured seconds", s.timedOut() && s.idleSeconds() >= 1);
        s.touch();
        CHECK("security: touch() resets idle time", !s.timedOut());
        CHECK("security: no cooldown for the first two failures", !s.recordFailure() && s.lockoutRemaining() == 0 && !s.recordFailure() && s.lockoutRemaining() == 0);
        s.recordFailure();
        CHECK("security: cooldown starts on the third failure", s.lockoutRemaining() > 0 && s.lockoutRemaining() <= 5);
        s.resetFailures();
        CHECK("security: resetFailures clears the cooldown", s.failures() == 0 && s.lockoutRemaining() == 0);
#ifndef NDEBUG
#ifdef _WIN32
        _putenv_s("SECUREVAULT_AUTOLOCK_SECONDS", "7");
#else
        setenv("SECUREVAULT_AUTOLOCK_SECONDS", "7", 1);
#endif

        CHECK("security: debug override SECUREVAULT_AUTOLOCK_SECONDS",
              SecurityManager(300).autoLockSeconds() == 7);

#ifdef _WIN32
        _putenv_s("SECUREVAULT_AUTOLOCK_SECONDS", "");
#else
        unsetenv("SECUREVAULT_AUTOLOCK_SECONDS");
#endif
#endif
        CHECK("security: default is 300 s", SecurityManager().autoLockSeconds() == 300);
    }

    // ---- Instance lock.
    fresh();
    {
        InstanceLock a, b;
        CHECK("instance lock: first acquire succeeds, second is refused", a.tryAcquire() && !b.tryAcquire());
    }
    {
        InstanceLock c;
        CHECK("instance lock: released when the holder goes away", c.tryAcquire());
    }

    // ---- AuthService end to end.
    fresh();
    {
        std::unique_ptr<Session> s;
        CHECK("auth: no accounts at first", !AuthService::anyUserExists());
        CHECK("auth: invalid username rejected", AuthService::registerUser("a b", "Tr0ub4dor&3xKcd!", s) == RegisterResult::InvalidUsername && AuthService::registerUser("ab", "Tr0ub4dor&3xKcd!", s) == RegisterResult::InvalidUsername);
        CHECK("auth: weak master passwords rejected", AuthService::registerUser("siam", "Password1234!", s) == RegisterResult::WeakPassword && !s);
        CHECK("auth: register succeeds and returns an unlocked session with a saved vault",
              AuthService::registerUser("siam", "Tr0ub4dor&3xKcd!", s) == RegisterResult::Ok && s && !s->isLocked() && fs::exists("data/siam_vault.sv"));
        CHECK("auth: duplicate username rejected", [&]
              { std::unique_ptr<Session> t; return AuthService::registerUser("siam", "Tr0ub4dor&3xKcd!", t) == RegisterResult::UsernameTaken; }());
        CHECK("auth: kdfIterations reports 600000 for the account, 0 for unknown", AuthService::kdfIterations("siam") == 600000 && AuthService::kdfIterations("nobody") == 0);
        s->vault().addPassword(entry("Keep", "k.com", "u", "Zq!9xWvLk2#Rt"));
        s->lock();
        CHECK("session: lock() destroys the vault and further access throws", s->isLocked() && [&]
                                                                                  { try { s->vault(); } catch (const std::logic_error &) { return true; } return false; }());

        std::unique_ptr<Session> t;
        CHECK("auth: wrong password -> InvalidCredentials, no session", AuthService::login("siam", "wrong-password-123", t) == LoginResult::InvalidCredentials && !t);
        auto t0 = std::chrono::steady_clock::now();
        auto r = AuthService::login("ghost", "wrong-password-123", t);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK("auth: unknown user is indistinguishable and pays key-derivation time", r == LoginResult::InvalidCredentials && ms >= 50);
        CHECK("audit: LOGIN_FAILED is written for existing users only", slurp("data/siam_audit.log").find("LOGIN_FAILED") != std::string::npos && !fs::exists("data/ghost_audit.log"));
        CHECK("auth: login succeeds and the vault is readable", AuthService::login("siam", "Tr0ub4dor&3xKcd!", t, {}, LoginPurpose::Unlock) == LoginResult::Ok && t->vault().getPasswords().size() == 1);
        CHECK("audit: VAULT_UNLOCK and LOCK_VAULT recorded, no password in the log", [&]
              { auto l = slurp("data/siam_audit.log"); return l.find("VAULT_UNLOCK") != std::string::npos && l.find("LOCK_VAULT") != std::string::npos && l.find("Zq!9xWvLk2#Rt") == std::string::npos; }());
        CHECK("auth: verifyMasterPassword", AuthService::verifyMasterPassword(*t, "Tr0ub4dor&3xKcd!") && !AuthService::verifyMasterPassword(*t, "nope"));

        CHECK("change: wrong current password refused", AuthService::changeMasterPassword(*t, "nope", "Xk7#mQ9!vLz2Wr4") == ChangeResult::WrongPassword);
        CHECK("change: weak new password refused", AuthService::changeMasterPassword(*t, "Tr0ub4dor&3xKcd!", "Password1234!") == ChangeResult::WeakPassword);
        std::string backupBefore = (fs::current_path() / "pre.sv").string();
        t->vault().backup(backupBefore);
        CHECK("change: succeeds", AuthService::changeMasterPassword(*t, "Tr0ub4dor&3xKcd!", "Xk7#mQ9!vLz2Wr4") == ChangeResult::Ok);
        CHECK("change: session keeps working (verify with the new password, add data)", AuthService::verifyMasterPassword(*t, "Xk7#mQ9!vLz2Wr4") && t->vault().addPassword(entry("After", "a.com", "u", "Hn4$vB8!qZr2Wm6")));
        CHECK("change: no rollback files left behind", !fs::exists("data/siam_vault.sv.rollback") && !fs::exists("data/users.db.rollback") && !fs::exists("data/users.db.tmp"));
        CHECK("change: the old backup can no longer be restored (documented limitation)", !t->vault().restore(backupBefore));
        CHECK("audit: CHANGE_MASTER_PASSWORD recorded", slurp("data/siam_audit.log").find("CHANGE_MASTER_PASSWORD") != std::string::npos);
        t->lock();
        std::unique_ptr<Session> u;
        CHECK("change: old password no longer works, new one does and data is intact",
              AuthService::login("siam", "Tr0ub4dor&3xKcd!", u) == LoginResult::InvalidCredentials && AuthService::login("siam", "Xk7#mQ9!vLz2Wr4", u) == LoginResult::Ok && u->vault().getPasswords().size() == 2);

        // Backup and restore through a live session.
        CHECK("backup/restore through the session", u->vault().backup("bare.sv") && u->vault().addPassword(entry("Extra", "x.com", "u", "Zq!9xWvLk2#Rt9")) && u->vault().restore("bare.sv") && u->vault().getPasswords().size() == 2);

        // Corrupt the vault: login reports VaultUnreadable and the session refuses to write.
        u->lock();
        std::ofstream("data/siam_vault.sv", std::ios::trunc) << "garbage";
        const std::string garbage = slurp("data/siam_vault.sv");
        std::unique_ptr<Session> w;
        CHECK("unreadable: login returns VaultUnreadable with a write-refusing session", AuthService::login("siam", "Xk7#mQ9!vLz2Wr4", w) == LoginResult::VaultUnreadable && w && !w->vault().isReady() && !w->vault().addPassword(entry("X", "x.com", "u", "pw")));
        w->lock();
        CHECK("unreadable: the file is untouched after lock()", slurp("data/siam_vault.sv") == garbage);
    }

    // ---- A users.db written by the ORIGINAL version (domain-separated salts, stored iteration count).
    fresh();
    {
        const std::string pw = "Tr0ub4dor&3xKcd!";
        const unsigned it = 1000;
        auto salt = Crypto::randomBytes(16);
        auto sa = salt;
        for (char c : std::string("AUTH"))
            sa.push_back((unsigned char)c);
        auto sv = salt;
        for (char c : std::string("VAULT"))
            sv.push_back((unsigned char)c);
        auto ak = Crypto::pbkdf2Sha256(pw, sa, it, 32);
        auto vk = Crypto::pbkdf2Sha256(pw, sv, it, 32);
        fs::create_directories("data");
        std::ofstream("data/users.db") << Utils::joinEscaped({"olduser", Crypto::hexEncode(salt), Crypto::hexEncode(Crypto::sha256(ak)), std::to_string(it)}) << "\n";
        std::ofstream("data/olduser_vault.sv") << Crypto::encryptAesGcm("SV2\nP|1|Legacy|l.com|u|pw|n|Other|0|1700000000|1700000000|0\n", vk);
        std::unique_ptr<Session> s;
        CHECK("legacy account (original users.db and vault) logs in with its stored iteration count",
              AuthService::login("olduser", pw, s) == LoginResult::Ok && s->vault().getPasswords().size() == 1 && s->kdfIterations() == 1000);
        CHECK("legacy account can change its master password", AuthService::changeMasterPassword(*s, pw, "Xk7#mQ9!vLz2Wr4") == ChangeResult::Ok && AuthService::kdfIterations("olduser") == 600000);
    }

    // ---- Clipboard outside Windows.
#ifndef _WIN32
    CHECK("clipboard: copy() is unavailable off Windows and clear() is harmless", !ClipboardManager::copy("x") && !ClipboardManager::copyWithAutoClear("x", 1) && (ClipboardManager::clear(), true));
#endif

    std::cout << "\n"
              << (total - failed) << "/" << total << " checks passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
