// SecureVault backend regression suite (framework-free).
//
// PURPOSE  Objective acceptance criteria for "Phase 0" of the UI brief.
//          Against the ORIGINAL code the checks marked [P0]/[P1] FAIL; after the
//          Phase 0 work every check must pass. Checks marked [guard] must pass
//          before AND after (they protect existing behaviour and file compatibility).
//
// BUILD    cmake -S . -B build && cmake --build build && ctest --test-dir build
//          or: g++ -std=c++17 -Iinclude tests/backend_regression_tests.cpp src/*.cpp -lcrypto -lpthread -o backend_tests
//
// NOTE     Changed from the supplied file in one place only: fresh() now points the data
//          directory at the scratch folder with Utils::setDataDir() (as the original note asked),
//          and the dataDir() check clears that override so it exercises the real resolution.
#include "FileVault.h"
#include "PasswordGenerator.h"
#include "Crypto.h"
#include "Utils.h"
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <filesystem>
namespace fs = std::filesystem;

static int failed = 0, total = 0;
#define CHECK(tag, label, cond) do { ++total; bool ok_ = (cond); \
    std::cout << (ok_ ? "[ok]    " : "[FAIL]  ") << tag << " " << label << "\n"; if (!ok_) ++failed; } while (0)

static std::vector<unsigned char> key(unsigned char b) { return std::vector<unsigned char>(32, b); }
static std::string slurp(const std::string &p) { std::ifstream f(p, std::ios::binary); std::ostringstream o; o << f.rdbuf(); return o.str(); }
static PasswordEntry entry(const std::string &t, const std::string &s, const std::string &u, const std::string &p) {
    PasswordEntry e; e.title = t; e.site = s; e.username = u; e.password = p; e.createdAt = e.updatedAt = std::time(nullptr); return e; }
static void fresh() {
    fs::current_path(fs::temp_directory_path()); fs::remove_all("sv_regress"); fs::create_directories("sv_regress");
    fs::current_path("sv_regress"); Utils::setDataDir(fs::current_path() / "data"); }

int main() {
    fresh();

    // ---- Compatibility fixture: a vault written by the ORIGINAL serializer must always load.
    {
        std::string legacy = "SV2\n"
            "P|1|GitHub|github.com|me@x.com|pw\\|with\\|pipes|n|Dev|1|1700000000|1700000000|1|oldpw|1690000000\n"
            "N|2|Note|content|Notes|0|1700000000|1700000000\n"
            "C|3|Card|HOLDER|4111111111111111|12/30|123|n|0|1700000000|1700000000\n";
        fs::create_directories("data");
        std::ofstream("data/legacy_vault.sv") << Crypto::encryptAesGcm(legacy, key(7));
        FileVault v("legacy", key(7));
        bool ok = v.load() && v.getPasswords().size() == 1 && v.getNotes().size() == 1 && v.getCards().size() == 1;
        CHECK("[guard]", "legacy-format vault loads (passwords, notes, cards)", ok);
        CHECK("[guard]", "pipes inside legacy password decode correctly", ok && v.getPasswords()[0].password == "pw|with|pipes");
        CHECK("[guard]", "legacy password history preserved", ok && v.getPasswords()[0].history.size() == 1);
    }

    // ---- P0: multi-line text must survive a save/load cycle (GUI note editor is multi-line).
    fresh();
    {
        { FileVault v("t", key(1)); v.load();
          SecureNote n; n.title = "Recovery"; n.content = "line one\nline two\r\nline three"; n.createdAt = n.updatedAt = std::time(nullptr); v.addNote(n);
          PasswordEntry e = entry("T", "s.com", "u", "pw"); e.notes = "first\nsecond"; v.addPassword(e);
          CardItem c; c.title = "C"; c.number = "4111111111111111"; c.notes = "a\nb"; c.createdAt = c.updatedAt = std::time(nullptr); v.addCard(c); }
        FileVault r("t", key(1)); r.load();
        CHECK("[P0]", "multi-line secure note survives reload", r.getNotes().size() == 1 && r.getNotes()[0].content == "line one\nline two\r\nline three");
        CHECK("[P0]", "multi-line credential notes survive reload (credential not lost)", r.getPasswords().size() == 1 && r.getPasswords()[0].notes == "first\nsecond");
        CHECK("[P0]", "multi-line card notes survive reload (card not lost)", r.getCards().size() == 1 && r.getCards()[0].notes == "a\nb");
    }

    // ---- P0: a failed load must never be followed by a write that destroys the vault file.
    fresh();
    {
        { FileVault v("t", key(1)); v.load(); v.addPassword(entry("Keep", "k.com", "u", "pw")); }
        std::string before = slurp("data/t_vault.sv");
        FileVault bad("t", key(2)); bool loaded = bad.load();
        bad.addPassword(entry("Oops", "o.com", "u", "pw2"));
        CHECK("[P0]", "failed load: vault file byte-identical after attempted write", !loaded && slurp("data/t_vault.sv") == before);
        FileVault orig("t", key(1));
        CHECK("[P0]", "failed load: original key still opens the vault", orig.load() && orig.getPasswords().size() == 1);
    }

    // ---- P0: update calls must not let a partial DTO destroy stored metadata.
    fresh();
    {
        FileVault v("t", key(1)); v.load(); v.addPassword(entry("S", "s.com", "u", "Old!Pass12345aa"));
        int id = v.getPasswords()[0].id;
        auto e = *v.findPassword(id); e.password = "New!Pass12345bb"; v.updatePassword(e);
        e = *v.findPassword(id); e.password = "Newer!Pass1234cc"; v.updatePassword(e);
        PasswordEntry dto = *v.findPassword(id); dto.history.clear(); dto.createdAt = 0; dto.title = "Renamed"; v.updatePassword(dto);
        CHECK("[P0]", "updatePassword keeps stored history when caller sends none", v.findPassword(id)->history.size() == 2);
        CHECK("[P0]", "updatePassword keeps stored createdAt", v.findPassword(id)->createdAt != 0);
        SecureNote n; n.title = "N"; n.content = "c"; n.createdAt = n.updatedAt = std::time(nullptr); v.addNote(n);
        int nid = v.getNotes()[0].id; SecureNote nd = *v.findNote(nid); nd.createdAt = 0; v.updateNote(nd);
        CHECK("[P0]", "updateNote keeps stored createdAt", v.findNote(nid)->createdAt != 0);
        CardItem c; c.title = "C"; c.number = "4111111111111111"; c.createdAt = c.updatedAt = std::time(nullptr); v.addCard(c);
        int cid = v.getCards()[0].id; CardItem cd = *v.findCard(cid); cd.createdAt = 0; v.updateCard(cd);
        CHECK("[P0]", "updateCard keeps stored createdAt", v.findCard(cid)->createdAt != 0);
    }
#ifdef SV_HAS_PASSWORD_CHANGED_AT   // defined by PasswordEntry.h now that passwordChangedAt exists
    {
        fresh(); FileVault v("t", key(1)); v.load();
        auto e = entry("Ancient", "a.com", "u", "Old!Pass12345aa"); e.passwordChangedAt = std::time(nullptr) - 400 * 86400; v.addPassword(e);
        int id = v.getPasswords()[0].id; auto f = *v.findPassword(id); f.favorite = true; v.updatePassword(f);
        CHECK("[P0]", "toggling favourite does not reset password age", std::time(nullptr) - v.findPassword(id)->passwordChangedAt > 180 * 86400);
    }
#endif

    // ---- P0: uniqueness rule must not block legitimate data.
    fresh();
    {
        FileVault v("t", key(1)); v.load();
        v.addPassword(entry("GitHub", "github.com", "a@x.com", "Zzz!k29dkQ#lmn"));
        CHECK("[P0]", "same website, different username is allowed", v.addPassword(entry("GitHub 2", "github.com", "b@x.com", "Qk29!dd0WxZlmnop")));
        CHECK("[P0]", "same website AND username is rejected", !v.addPassword(entry("Dup", "GitHub.com", "A@x.com", "Pk39!dd0WxZlmnop")));
        bool a = v.addPassword(entry("Wi-Fi", "", "", "Ak29!dd0WxZlmnop")), b = v.addPassword(entry("Router", "", "", "Bk39!dd0WxZlmnop"));
        CHECK("[P0]", "multiple credentials with no website are allowed", a && b);
    }

    // ---- P0: data directory must not depend on the current working directory.
    {
        Utils::setDataDir({});   // use the real resolution: option/env -> executable folder -> per-user folder
        std::string d1 = Utils::dataDir().string(); fs::create_directories("/tmp/sv_elsewhere");
        fs::path keep = fs::current_path(); fs::current_path("/tmp/sv_elsewhere"); std::string d2 = Utils::dataDir().string(); fs::current_path(keep);
        CHECK("[P0]", "dataDir() identical from any working directory", d1 == d2);
        Utils::setDataDir(fs::current_path() / "data");
    }

    // ---- P0: passphrases need a real word list (original: 59 distinct words, ~5.9 bits/word).
    {
        PasswordGenerator g; std::set<std::string> words;
        for (int i = 0; i < 40000; ++i) { std::string p = g.generatePassphrase(4); size_t s = 0;
            while (true) { size_t d = p.find('-', s); words.insert(p.substr(s, d == std::string::npos ? d : d - s)); if (d == std::string::npos) break; s = d + 1; } }
        std::cout << "         distinct passphrase words observed: " << words.size() << " (~" << std::log2((double)words.size()) << " bits/word)\n";
        CHECK("[P0]", "passphrase word list has >= 1500 distinct words (>= ~10.5 bits/word)", words.size() >= 1500);
        PasswordGeneratorOptions o; o.length = 20;
        CHECK("[guard]", "generator: 20-char default works, 7 and 129 rejected, no class selected rejected",
              g.generate(o).size() == 20 && g.generate({7,1,1,1,1,1}).empty() && g.generate({129,1,1,1,1,1}).empty() && g.generate({20,0,0,0,0,1}).empty());
    }

    // ---- P1: backup / restore behaviour.
    fresh();
    {
        FileVault v("t", key(1)); v.load(); v.addPassword(entry("T", "t.com", "u", "pw"));
        CHECK("[P1]", "backup() accepts a bare filename (no folder)", v.backup("bare_backup.sv"));
        fs::create_directories("data/backups");
        CHECK("[guard]", "backup() into a folder works", v.backup("data/backups/b1.sv"));
        std::ofstream("data/backups/garbage.sv") << "not a vault";
        CHECK("[guard]", "restore() rejects garbage and keeps current vault", !v.restore("data/backups/garbage.sv") && v.getPasswords().size() == 1);
        std::string before = slurp("data/t_vault.sv");
        CHECK("[guard]", "restore() of a valid backup succeeds", v.restore("data/backups/b1.sv"));
        (void)before;
        CHECK("[guard]", "rekey() works and data is intact afterwards", v.rekey(key(9)) && v.getPasswords().size() == 1 && FileVault("t", key(9)).load());
        // KNOWN LIMITATION (documented, not a failure): backups made before rekey() cannot be restored
        // because the old salt is gone. The UI must warn about this; see brief section on master password.
        std::cout << "         info: restore of pre-rekey backup -> " << (v.restore("data/backups/b1.sv") ? "restored" : "refused (expected limitation)") << "\n";
    }

    std::cout << "\n" << (total - failed) << "/" << total << " checks passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
