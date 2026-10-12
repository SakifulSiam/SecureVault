#include "AuditLogger.h"
#include "Utils.h"
#include <fstream>
#include <sstream>
AuditLogger::AuditLogger(std::string file) : filename(std::move(file)) { load(); }
void AuditLogger::load()
{
    events.clear();
    std::ifstream f(filename);
    std::string line;
    while (std::getline(f, line))
    {
        auto p = Utils::splitEscaped(line);
        if (p.size() >= 3)
        {
            try
            {
                events.push_back({(std::time_t)std::stoll(p[0]), p[1], p[2]});
            }
            catch (...)
            {
            }
        }
    }
}
void AuditLogger::log(const std::string &action, const std::string &detail)
{
    AuditEvent e{std::time(nullptr), action, detail};
    events.push_back(e);
    std::ofstream f(filename, std::ios::app);
    if (f)
        f << Utils::joinEscaped({std::to_string(e.timestamp), e.action, e.detail}) << "\n";
}
const std::vector<AuditEvent> &AuditLogger::getEvents() const { return events; }
