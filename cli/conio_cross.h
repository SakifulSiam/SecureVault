#pragma once
#include <iostream>
#include <string>
#include <cstdio>
#include <cctype>
#ifdef _WIN32
#include <conio.h>
inline char getch_cross() { return _getch(); }
inline void clear_stdin_buffer() {}
constexpr char KEY_ENTER = '\r';
constexpr char KEY_BACKSPACE = '\b';
#else
#include <termios.h>
#include <unistd.h>
constexpr char KEY_ENTER = '\n';
constexpr char KEY_BACKSPACE = 127;
inline void clear_stdin_buffer() {}
inline char getch_cross()
{
    char ch;
    termios oldt{}, newt{};
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    if (read(STDIN_FILENO, &ch, 1) != 1)
        ch = KEY_ENTER; // end of input: finish the line instead of looping forever
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
#endif
