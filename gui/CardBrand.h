#pragma once
#include <string>
// Presentation only: never stored. Pure function so it can be unit-tested.
namespace CardBrand {
inline std::string fromNumber(const std::string &raw)
{
    std::string d; for (char c : raw) if (c >= '0' && c <= '9') d += c;
    auto pre = [&](size_t n) { return d.size() >= n ? std::stoi(d.substr(0, n)) : -1; };
    if (d.empty()) return "Card";
    if (d[0] == '4') return "Visa";
    if ((pre(2) >= 51 && pre(2) <= 55) || (pre(4) >= 2221 && pre(4) <= 2720)) return "Mastercard";
    if (pre(2) == 34 || pre(2) == 37) return "American Express";
    if (pre(4) == 6011 || pre(2) == 65 || (pre(3) >= 644 && pre(3) <= 649)) return "Discover";
    return "Card";
}
inline std::string last4(const std::string &raw)
{
    std::string d; for (char c : raw) if (c >= '0' && c <= '9') d += c;
    return d.size() >= 4 ? d.substr(d.size() - 4) : d;
}
}
