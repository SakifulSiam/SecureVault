#pragma once
#include <string>
struct PasswordAssessment
{
    int score = 0;
    std::string label;          // raw label from the score; the UI must use displayLabel()
    bool weak = true;
    bool commonPattern = false;
    std::string weakReason;     // "" when not weak; otherwise "Common pattern" or "Too simple"
    // Always agrees with `weak`: a weak password is never shown as Medium/Strong (P0-10).
    std::string displayLabel() const { return weak ? "Weak" : label; }
};

// Master-password policy (P0-9). One function drives registration, change and the UI checklist.
struct MasterPasswordPolicy
{
    static constexpr int kMinLength = 12;
    bool lengthOk = false;      // at least kMinLength characters
    bool notWeak = false;       // !assess().weak: no common pattern and score >= 60
    bool ok = false;            // lengthOk && notWeak
    PasswordAssessment assessment;
    static MasterPasswordPolicy evaluate(const std::string &password);
};

class PasswordChecker
{
public:
    PasswordAssessment assess(const std::string &password) const;
    std::string checkStrength(const std::string &password) const;
};
