#pragma once
#include <string>
// Windows only. Elsewhere copy() returns false and clear() does nothing.
class ClipboardManager
{
public:
    // UTF-8 text, placed as Unicode text and marked as excluded from clipboard history and cloud sync.
    static bool copy(const std::string &text);
    // copy(), then clears the clipboard after `seconds` - but only if it still holds our text.
    static bool copyWithAutoClear(const std::string &text, int seconds);
    // Clears the clipboard only if it still holds text we put there. Used by lock and exit.
    static void clear();
};
