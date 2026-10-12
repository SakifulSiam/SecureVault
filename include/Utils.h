#pragma once
#include <string>
#include <vector>
#include <ctime>
#include <filesystem>
#include <optional>

// Qt-free, console-free helpers. Console input lives in cli/Console.h.
namespace Utils
{
    // Data folder, resolved once and cached. Order (P0-6):
    //   1. setDataDir() / --data-dir option, then SECUREVAULT_DATA_DIR
    //   2. <folder of the executable>/data
    //   3. per-user app-data folder when the executable folder is not writable
    std::filesystem::path dataDir();
    // Override for CLI option and tests. An empty path clears the override and the cache.
    void setDataDir(const std::filesystem::path &dir);
    // An older ./data (current working directory) that holds users.db while the resolved
    // folder does not. The caller must ASK before using it; nothing is ever copied silently.
    std::optional<std::filesystem::path> legacyWorkingDirData();

    std::filesystem::path usersFile();
    std::filesystem::path auditFile(const std::string &username);
    std::filesystem::path vaultFile(const std::string &username);
    std::string lower(std::string s);
    std::string trim(const std::string &s);
    // Escapes '\', '|', LF and CR (LF/CR as \n and \r). Legacy files never contain \n or \r
    // escapes, so every file written by the original version still decodes identically.
    std::string joinEscaped(const std::vector<std::string> &fields);
    std::vector<std::string> splitEscaped(const std::string &line);
    std::string timeString(std::time_t t);
    std::string dateString(std::time_t t);
    bool hasLineBreak(const std::string &s);
}
