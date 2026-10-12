#include "ClipboardManager.h"
#include <atomic>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>

namespace
{
    std::atomic<DWORD> g_ourSequence{0};
    std::atomic<unsigned> g_generation{0};

    bool setDwordFormat(UINT fmt, DWORD value)
    {
        if (!fmt)
            return false;
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (!h)
            return false;
        if (void *p = GlobalLock(h))
        {
            *static_cast<DWORD *>(p) = value;
            GlobalUnlock(h);
        }
        if (!SetClipboardData(fmt, h))
        {
            GlobalFree(h);
            return false;
        }
        return true;
    }
}

bool ClipboardManager::copy(const std::string &text)
{
    int wlen = text.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    if (!text.empty() && wlen <= 0)
        return false;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, ((size_t)wlen + 1) * sizeof(wchar_t));
    if (!h)
        return false;
    wchar_t *w = static_cast<wchar_t *>(GlobalLock(h));
    if (!w)
    {
        GlobalFree(h);
        return false;
    }
    if (wlen)
        MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), w, wlen);
    w[wlen] = L'\0';
    GlobalUnlock(h);

    if (!OpenClipboard(nullptr))
    {
        GlobalFree(h);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, h))
    {
        GlobalFree(h);
        CloseClipboard();
        return false;
    }
    // Keep secrets out of Win+V history and cloud clipboard.
    setDwordFormat(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"), 1);
    setDwordFormat(RegisterClipboardFormatW(L"CanIncludeInClipboardHistory"), 0);
    setDwordFormat(RegisterClipboardFormatW(L"CanUploadToCloudClipboard"), 0);
    CloseClipboard();
    g_ourSequence = GetClipboardSequenceNumber();
    ++g_generation;
    return true;
}

bool ClipboardManager::copyWithAutoClear(const std::string &text, int seconds)
{
    if (!copy(text))
        return false;
    const unsigned gen = g_generation.load();
    std::thread([gen, seconds]
                {
        for (int i = 0; i < seconds * 4; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (g_generation.load() != gen)
                return; // a newer copy or an explicit clear took over
        }
        clear(); })
        .detach();
    return true;
}

void ClipboardManager::clear()
{
    DWORD ours = g_ourSequence.load();
    if (!ours)
        return;
    if (!OpenClipboard(nullptr))
        return;
    if (GetClipboardSequenceNumber() == ours) // still our data
        EmptyClipboard();
    CloseClipboard();
    g_ourSequence = 0;
    ++g_generation;
}
#else
bool ClipboardManager::copy(const std::string &) { return false; }
bool ClipboardManager::copyWithAutoClear(const std::string &, int) { return false; }
void ClipboardManager::clear() {}
#endif
