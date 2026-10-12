#include "../CardBrand.h"
#include <iostream>
static int failed = 0;
static void check(const char *label, bool ok) { std::cout << (ok ? "[ok]   " : "[FAIL] ") << label << "\n"; if (!ok) ++failed; }
int main()
{
    using namespace CardBrand;
    check("visa", fromNumber("4111 1111 1111 1111") == "Visa");
    check("mastercard 51-55", fromNumber("5555555555554444") == "Mastercard");
    check("mastercard 2221-2720", fromNumber("2223003122003222") == "Mastercard" && fromNumber("2720990000000000") == "Mastercard");
    check("2221 boundary below is not mastercard", fromNumber("2220999999999999") == "Card");
    check("amex 34 and 37", fromNumber("378282246310005") == "American Express" && fromNumber("341111111111111") == "American Express");
    check("discover", fromNumber("6011111111111117") == "Discover" && fromNumber("6500000000000000") == "Discover");
    check("empty and short input", fromNumber("") == "Card" && fromNumber("5") == "Card" && fromNumber("2") == "Card");
    check("separators ignored", fromNumber("4111-1111-1111-1111") == "Visa");
    check("last4", last4("4111 1111 1111 1234") == "1234" && last4("12") == "12" && last4("") == "");
    std::cout << (failed ? "FAILED\n" : "all passed\n");
    return failed ? 1 : 0;
}
