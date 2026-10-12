#include "Vault.h"
#include "Utils.h"
#include <algorithm>
#include <set>

bool Vault::doesSiteExist(const std::string &name)
{
    for (const auto &e : entries)
        if (Utils::lower(e.site) == Utils::lower(name))
            return true;
    return false;
}
bool Vault::isDuplicateCredential(const std::string &site, const std::string &username, int excludeId) const
{
    if (site.empty())
        return false;
    const std::string s = Utils::lower(site), u = Utils::lower(username);
    for (const auto &e : entries)
        if (e.id != excludeId && Utils::lower(e.site) == s && Utils::lower(e.username) == u)
            return true;
    return false;
}
PasswordEntry *Vault::findPassword(int id)
{
    for (auto &e : entries)
        if (e.id == id)
            return &e;
    return nullptr;
}
SecureNote *Vault::findNote(int id)
{
    for (auto &e : notes)
        if (e.id == id)
            return &e;
    return nullptr;
}
CardItem *Vault::findCard(int id)
{
    for (auto &e : cards)
        if (e.id == id)
            return &e;
    return nullptr;
}
std::vector<PasswordEntry> Vault::searchPasswords(const std::string &query) const
{
    std::vector<PasswordEntry> r;
    for (int id : searchPasswordIds(query))
        for (const auto &e : entries)
            if (e.id == id)
                r.push_back(e);
    return r;
}
std::vector<PasswordEntry> Vault::favorites() const
{
    std::vector<PasswordEntry> r;
    for (const auto &e : entries)
        if (e.favorite)
            r.push_back(e);
    return r;
}
std::vector<int> Vault::searchPasswordIds(const std::string &query) const
{
    std::vector<int> r;
    const auto q = Utils::lower(query);
    for (const auto &e : entries)
    {
        std::string s = Utils::lower(e.title + " " + e.site + " " + e.username + " " + e.category + " " + e.notes);
        if (s.find(q) != std::string::npos)
            r.push_back(e.id);
    }
    return r;
}
std::vector<int> Vault::searchNoteIds(const std::string &query) const
{
    std::vector<int> r;
    const auto q = Utils::lower(query);
    for (const auto &n : notes)
        if (Utils::lower(n.title + " " + n.category + " " + n.content).find(q) != std::string::npos)
            r.push_back(n.id);
    return r;
}
std::vector<int> Vault::searchCardIds(const std::string &query) const
{
    std::vector<int> r;
    const auto q = Utils::lower(query);
    for (const auto &c : cards)
        if (Utils::lower(c.title + " " + c.cardholder + " " + c.notes).find(q) != std::string::npos)
            r.push_back(c.id);
    return r;
}
std::vector<int> Vault::favoritePasswordIds() const
{
    std::vector<int> r;
    for (const auto &e : entries)
        if (e.favorite)
            r.push_back(e.id);
    return r;
}
std::vector<int> Vault::favoriteNoteIds() const
{
    std::vector<int> r;
    for (const auto &e : notes)
        if (e.favorite)
            r.push_back(e.id);
    return r;
}
std::vector<int> Vault::favoriteCardIds() const
{
    std::vector<int> r;
    for (const auto &e : cards)
        if (e.favorite)
            r.push_back(e.id);
    return r;
}
std::vector<std::string> Vault::categories() const
{
    std::set<std::string> seen;
    std::vector<std::string> out;
    auto add = [&](const std::string &c)
    {
        if (!c.empty() && seen.insert(Utils::lower(c)).second)
            out.push_back(c);
    };
    for (const auto &e : entries)
        add(e.category);
    for (const auto &n : notes)
        add(n.category);
    std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b)
              { return Utils::lower(a) < Utils::lower(b); });
    return out;
}
int Vault::nextId() const
{
    int m = 0;
    for (const auto &e : entries)
        m = std::max(m, e.id);
    for (const auto &e : notes)
        m = std::max(m, e.id);
    for (const auto &e : cards)
        m = std::max(m, e.id);
    return m + 1;
}
