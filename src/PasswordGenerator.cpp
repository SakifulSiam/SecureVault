#include "PasswordGenerator.h"
#include "Crypto.h"
#include "WordList.h"
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace
{
    unsigned char secureIndex(size_t upperExclusive)
    {
        if (upperExclusive == 0 || upperExclusive > 256)
            throw std::invalid_argument("Invalid random range");

        const unsigned limit = 256u - (256u % static_cast<unsigned>(upperExclusive));
        while (true)
        {
            unsigned char b = Crypto::randomBytes(1)[0];
            if (b < limit)
                return static_cast<unsigned char>(b % upperExclusive);
        }
    }

    char choose(const std::string &pool)
    {
        return pool[secureIndex(pool.size())];
    }
}

namespace
{
    struct Pools
    {
        std::vector<std::string> required;
        std::string pool;
    };
    Pools buildPools(const PasswordGeneratorOptions &o)
    {
        std::string lower = "abcdefghijklmnopqrstuvwxyz";
        std::string upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::string digits = "0123456789";
        std::string symbols = "!@#$%^&*()-_=+[]{}?";
        if (o.avoidAmbiguous)
        {
            for (char c : std::string("O0Il1|"))
            {
                lower.erase(std::remove(lower.begin(), lower.end(), c), lower.end());
                upper.erase(std::remove(upper.begin(), upper.end(), c), upper.end());
                digits.erase(std::remove(digits.begin(), digits.end(), c), digits.end());
                symbols.erase(std::remove(symbols.begin(), symbols.end(), c), symbols.end());
            }
        }
        Pools p;
        if (o.lower && !lower.empty()) { p.required.push_back(lower); p.pool += lower; }
        if (o.upper && !upper.empty()) { p.required.push_back(upper); p.pool += upper; }
        if (o.digits && !digits.empty()) { p.required.push_back(digits); p.pool += digits; }
        if (o.symbols && !symbols.empty()) { p.required.push_back(symbols); p.pool += symbols; }
        return p;
    }

    // Uniform index in [0, n) for n <= 65536 using rejection sampling over 16-bit values.
    size_t secureIndexWide(size_t n)
    {
        if (n == 0 || n > 65536)
            throw std::invalid_argument("Invalid random range");
        const unsigned limit = 65536u - (65536u % static_cast<unsigned>(n));
        while (true)
        {
            auto b = Crypto::randomBytes(2);
            unsigned v = (static_cast<unsigned>(b[0]) << 8) | b[1];
            if (v < limit)
                return v % n;
        }
    }
}

int PasswordGenerator::poolSize(const PasswordGeneratorOptions &o)
{
    if (o.length < 8 || o.length > 128)
        return 0;
    Pools p = buildPools(o);
    if (p.pool.empty() || static_cast<int>(p.required.size()) > o.length)
        return 0;
    return static_cast<int>(p.pool.size());
}

std::size_t PasswordGenerator::wordListSize() { return WordList::kCount; }

std::string PasswordGenerator::generate(const PasswordGeneratorOptions &o) const
{
    if (o.length < 8 || o.length > 128)
        return "";
    Pools p = buildPools(o);
    if (p.pool.empty() || static_cast<int>(p.required.size()) > o.length)
        return "";

    std::string out;
    out.reserve(o.length);
    for (const auto &group : p.required)
        out += choose(group);
    while (static_cast<int>(out.size()) < o.length)
        out += choose(p.pool);

    // Fisher-Yates with cryptographically secure random indices.
    for (size_t i = out.size(); i > 1; --i)
    {
        size_t j = secureIndex(i);
        std::swap(out[i - 1], out[j]);
    }
    return out;
}

std::string PasswordGenerator::generatePassphrase(int words) const
{
    if (words < 3 || words > 12)
        return "";
    std::string out;
    for (int i = 0; i < words; ++i)
    {
        if (i)
            out += '-';
        out += WordList::kWords[secureIndexWide(WordList::kCount)];
    }
    return out;
}
