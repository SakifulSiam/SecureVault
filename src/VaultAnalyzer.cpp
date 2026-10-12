#include "VaultAnalyzer.h"
#include "Crypto.h"
#include "PasswordChecker.h"
#include <algorithm>
#include <map>

std::string VaultAnalyzer::bandFor(int score)
{
    return score >= 90 ? "Excellent" : score >= 75 ? "Good" : score >= 60 ? "Needs attention" : "Weak";
}

HealthReport VaultAnalyzer::analyze(const Vault &vault, std::time_t now)
{
    HealthReport r;
    PasswordChecker checker;
    std::map<std::string, std::vector<int>> byHash; // SHA-256 of the password, so no password copies are kept
    for (const auto &e : vault.getPasswords())
    {
        CredentialFlags f;
        f.id = e.id;
        f.weak = checker.assess(e.password).weak;
        const std::time_t changed = e.passwordChangedAt ? e.passwordChangedAt : e.updatedAt;
        f.old = (long long)(now - changed) / 3600 > 24 * 180;
        byHash[Crypto::sha256Hex(e.password)].push_back(e.id);
        r.credentials.push_back(f);
    }
    for (auto &kv : byHash)
        if (kv.second.size() > 1)
            r.reuseGroups.push_back(kv.second);
    std::sort(r.reuseGroups.begin(), r.reuseGroups.end(), [](const auto &a, const auto &b)
              { return a.front() < b.front(); });
    for (const auto &g : r.reuseGroups)
        for (int id : g)
            for (auto &f : r.credentials)
                if (f.id == id)
                    f.reused = true;
    r.total = (int)r.credentials.size();
    for (const auto &f : r.credentials)
    {
        r.weak += f.weak;
        r.old += f.old;
        r.reused += f.reused;
        r.attentionCount += (f.weak || f.old || f.reused) ? 1 : 0;
    }
    r.strong = r.total - r.weak;
    r.hasData = r.total > 0;
    r.score = r.hasData ? std::max(0, 100 - r.weak * 10 - r.old * 5 - r.reused * 5) : 100;
    r.band = bandFor(r.score);
    return r;
}
