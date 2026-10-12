#include "Console.h"
#include "conio_cross.h"
#include "Utils.h"
#include <cctype>
#include <iostream>
#include <stdexcept>

namespace Console
{
    std::string inputLine(const std::string &prompt)
    {
        std::cout << prompt;
        std::string s;
        std::getline(std::cin, s);
        return s;
    }
    int inputInt(const std::string &prompt, int minValue, int maxValue)
    {
        while (true)
        {
            std::string s = inputLine(prompt);
            if (!std::cin)
                throw std::runtime_error("input closed");
            try
            {
                size_t p = 0;
                int v = std::stoi(s, &p);
                if (p == s.size() && v >= minValue && v <= maxValue)
                    return v;
            }
            catch (...)
            {
            }
            std::cout << "Invalid choice. Enter a value from " << minValue << " to " << maxValue << ".\n";
        }
    }
    bool yesNo(const std::string &prompt)
    {
        while (true)
        {
            std::string s = Utils::lower(inputLine(prompt + " (y/n): "));
            if (!std::cin)
                throw std::runtime_error("input closed");
            if (s == "y" || s == "yes")
                return true;
            if (s == "n" || s == "no")
                return false;
        }
    }
    std::string inputPassword(const std::string &prompt)
    {
        std::cout << prompt << std::flush;
        std::string p;
        clear_stdin_buffer();
        while (true)
        {
            char c = getch_cross();
            if (c == KEY_ENTER)
                break;
            if (c == KEY_BACKSPACE)
            {
                if (!p.empty())
                {
                    p.pop_back();
                    std::cout << "\b \b" << std::flush;
                }
            }
            else if (std::isprint((unsigned char)c))
            {
                p.push_back(c);
                std::cout << '*' << std::flush;
            }
        }
        std::cout << "\n";
        return p;
    }
}
