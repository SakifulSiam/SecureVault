#pragma once
#include <string>
// Terminal input helpers. Console-only; not part of securevault_core.
namespace Console
{
    std::string inputLine(const std::string &prompt);
    int inputInt(const std::string &prompt, int minValue, int maxValue);
    bool yesNo(const std::string &prompt);
    // Masked input ('*' per character).
    std::string inputPassword(const std::string &prompt = "Password: ");
}
