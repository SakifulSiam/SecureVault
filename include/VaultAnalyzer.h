#pragma once
#include "Vault.h"
#include <ctime>
#include <string>
#include <vector>

struct CredentialFlags
{
    int id = 0;
    bool weak = false;
    bool reused = false;
    bool old = false;   // password not changed for more than 180 days
};

// The exact health formula of the original CLI (showHealth), plus per-credential detail.
// Reuse groups hold ids only, never passwords.
struct HealthReport
{
    bool hasData = false;              // false for an empty vault (which still scores 100)
    int total = 0, strong = 0, weak = 0, reused = 0, old = 0;
    int score = 100;                   // total ? max(0, 100 - 10*weak - 5*old - 5*reused) : 100
    std::string band;                  // Excellent 90-100, Good 75-89, Needs attention 60-74, Weak below 60
    int attentionCount = 0;            // distinct credentials with at least one issue
    std::vector<CredentialFlags> credentials;
    std::vector<std::vector<int>> reuseGroups;
};

class VaultAnalyzer
{
public:
    static HealthReport analyze(const Vault &vault, std::time_t now = std::time(nullptr));
    static std::string bandFor(int score);
};
