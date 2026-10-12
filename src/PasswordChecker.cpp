#include "PasswordChecker.h"
#include "Utils.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
PasswordAssessment PasswordChecker::assess(const std::string &p) const
{
    int upper = 0, lower = 0, digit = 0, symbol = 0;
    for (unsigned char c : p)
    {
        if (std::isupper(c))
            upper++;
        else if (std::islower(c))
            lower++;
        else if (std::isdigit(c))
            digit++;
        else
            symbol++;
    }
    int score = 0;
    score += std::min(40, (int)p.size() * 2);
    score += (upper ? 15 : 0) + (lower ? 15 : 0) + (digit ? 15 : 0) + (symbol ? 15 : 0);
    if (p.size() > 20)
        score += 5;
    score = std::min(100, score);
    bool pattern = false;
    std::string q = Utils::lower(p);
    if (q.find("password") != std::string::npos || q.find("123456") != std::string::npos || q.find("qwerty") != std::string::npos)
        pattern = true;
    for (size_t i = 2; i < p.size(); ++i)
        if (p[i] == p[i - 1] && p[i] == p[i - 2])
            pattern = true;
    if (pattern)
        score = std::max(0, score - 25);
    PasswordAssessment a;
    a.score = score;
    a.commonPattern = pattern;
    a.weak = score < 60 || pattern;
    a.weakReason = !a.weak ? "" : pattern ? "Common pattern" : "Too simple";
    a.label = score >= 85 ? "Excellent" : score >= 70 ? "Strong"
                                      : score >= 50   ? "Medium"
                                                      : "Weak";
    return a;
}
MasterPasswordPolicy MasterPasswordPolicy::evaluate(const std::string &password)
{
    MasterPasswordPolicy r;
    r.assessment = PasswordChecker().assess(password);
    r.lengthOk = (int)password.size() >= kMinLength;
    r.notWeak = !r.assessment.weak;
    r.ok = r.lengthOk && r.notWeak;
    return r;
}
std::string PasswordChecker::checkStrength(const std::string &password) const { return assess(password).label; }
