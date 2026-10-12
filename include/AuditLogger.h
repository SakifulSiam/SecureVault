#pragma once
#include <string>
#include <vector>
#include <ctime>

struct AuditEvent {
    std::time_t timestamp{};
    std::string action;
    std::string detail;
};

class AuditLogger {
    std::string filename;
    std::vector<AuditEvent> events;
public:
    explicit AuditLogger(std::string file);
    void load();
    void log(const std::string& action, const std::string& detail = "");
    const std::vector<AuditEvent>& getEvents() const;
};
