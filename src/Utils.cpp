#include "Utils.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
    std::mutex g_mutex;
    fs::path g_override;
    fs::path g_cached;

    fs::path executableDir()
    {
#ifdef _WIN32
        std::wstring buf(MAX_PATH, L'\0');
        for (;;)
        {
            DWORD n = GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
            if (n == 0)
                return {};
            if (n < buf.size())
            {
                buf.resize(n);
                break;
            }
            buf.resize(buf.size() * 2);
        }
        return fs::path(buf).parent_path();
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buf(size, '\0');
        if (_NSGetExecutablePath(&buf[0], &size) != 0)
            return {};
        std::error_code ec;
        auto p = fs::canonical(fs::path(buf.c_str()), ec);
        return ec ? fs::path() : p.parent_path();
#else
        std::error_code ec;
        auto p = fs::read_symlink("/proc/self/exe", ec);
        return ec ? fs::path() : p.parent_path();
#endif
    }

    fs::path perUserDir()
    {
#ifdef _WIN32
        if (const char *a = std::getenv("APPDATA"))
            return fs::path(a) / "SecureVault";
#elif defined(__APPLE__)
        if (const char *h = std::getenv("HOME"))
            return fs::path(h) / "Library" / "Application Support" / "SecureVault";
#else
        if (const char *x = std::getenv("XDG_DATA_HOME"); x && *x)
            return fs::path(x) / "SecureVault";
        if (const char *h = std::getenv("HOME"))
            return fs::path(h) / ".local" / "share" / "SecureVault";
#endif
        return fs::temp_directory_path() / "SecureVault";
    }

    bool writableDir(const fs::path &dir)
    {
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec || !fs::is_directory(dir, ec))
            return false;
        const fs::path probe = dir / ".sv_write_probe";
        {
            std::ofstream f(probe, std::ios::trunc);
            if (!f)
                return false;
            f << "x";
            if (!f)
                return false;
        }
        fs::remove(probe, ec);
        return true;
    }

    fs::path resolve()
    {
        if (!g_override.empty())
            return g_override;
        if (const char *e = std::getenv("SECUREVAULT_DATA_DIR"); e && *e)
            return fs::path(e);
        fs::path exe = executableDir();
        if (!exe.empty() && writableDir(exe / "data"))
            return exe / "data";
        return perUserDir();
    }
}

namespace Utils
{
    fs::path dataDir()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_cached.empty())
            g_cached = resolve();
        std::error_code ec;
        fs::create_directories(g_cached, ec);
        return g_cached;
    }
    void setDataDir(const fs::path &dir)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_override = dir.empty() ? fs::path() : fs::absolute(dir);
        g_cached.clear();
    }
    std::optional<fs::path> legacyWorkingDirData()
    {
        std::error_code ec;
        fs::path legacy = fs::current_path(ec) / "data";
        if (ec || !fs::exists(legacy / "users.db", ec))
            return std::nullopt;
        fs::path active = dataDir();
        if (fs::equivalent(legacy, active, ec) || fs::exists(active / "users.db", ec))
            return std::nullopt;
        return legacy;
    }
    fs::path usersFile() { return dataDir() / "users.db"; }
    fs::path auditFile(const std::string &username) { return dataDir() / (username + "_audit.log"); }
    fs::path vaultFile(const std::string &username) { return dataDir() / (username + "_vault.sv"); }
    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return (char)std::tolower(c); });
        return s;
    }
    std::string trim(const std::string &s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a]))
            a++;
        while (b > a && std::isspace((unsigned char)s[b - 1]))
            b--;
        return s.substr(a, b - a);
    }
    bool hasLineBreak(const std::string &s) { return s.find_first_of("\r\n") != std::string::npos; }
    std::string joinEscaped(const std::vector<std::string> &fields)
    {
        std::string out;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (i)
                out += '|';
            for (char c : fields[i])
            {
                if (c == '\\' || c == '|')
                {
                    out += '\\';
                    out += c;
                }
                else if (c == '\n')
                    out += "\\n";
                else if (c == '\r')
                    out += "\\r";
                else
                    out += c;
            }
        }
        return out;
    }
    std::vector<std::string> splitEscaped(const std::string &line)
    {
        std::vector<std::string> out(1);
        bool esc = false;
        for (char c : line)
        {
            if (esc)
            {
                out.back() += (c == 'n') ? '\n' : (c == 'r') ? '\r' : c;
                esc = false;
            }
            else if (c == '\\')
                esc = true;
            else if (c == '|')
                out.emplace_back();
            else
                out.back() += c;
        }
        if (esc)
            out.back() += '\\';
        return out;
    }
    std::string timeString(std::time_t t)
    {
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream o;
        o << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return o.str();
    }
    std::string dateString(std::time_t t)
    {
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream o;
        o << std::put_time(&tm, "%Y-%m-%d");
        return o.str();
    }
}
