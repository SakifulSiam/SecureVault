#pragma once
#include <cstddef>
#include <string>
struct PasswordGeneratorOptions
{
    int length = 20;
    bool upper = true;
    bool lower = true;
    bool digits = true;
    bool symbols = true;
    bool avoidAmbiguous = true;
};
class PasswordGenerator
{
public:
    std::string generate(const PasswordGeneratorOptions &options) const;
    // 3-12 random words from the 2048-word list, joined by '-'. Default 5 words (~55 bits).
    std::string generatePassphrase(int words = 5) const;
    // Size of the character pool generate() draws from (0 for invalid options). Lets the UI show
    // exact bits for generated secrets: roughly length * log2(poolSize).
    static int poolSize(const PasswordGeneratorOptions &options);
    static std::size_t wordListSize();
};
