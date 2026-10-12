#include "FileVault.h"
#include "Crypto.h"
#include "Utils.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace
{
    constexpr size_t HISTORY_LIMIT = 5;

    std::string flag(bool b) { return b ? "1" : "0"; }
}

FileVault::FileVault(std::string u, std::vector<unsigned char> k) : username(std::move(u)), key(std::move(k))
{
    filename = Utils::vaultFile(username).string();
    backupDir = (Utils::dataDir() / "backups").string();
    std::error_code ec;
    fs::create_directories(backupDir, ec);
    crypto = std::make_shared<AesGcmEncryption>(key);
}

FileVault::~FileVault()
{
    wipeData();
    Crypto::wipe(key);
}

void FileVault::wipeData()
{
    for (auto &e : entries)
    {
        Crypto::wipe(e.password);
        Crypto::wipe(e.notes);
        for (auto &h : e.history)
            Crypto::wipe(h.password);
    }
    for (auto &n : notes)
        Crypto::wipe(n.content);
    for (auto &c : cards)
    {
        Crypto::wipe(c.number);
        Crypto::wipe(c.cvv);
        Crypto::wipe(c.notes);
    }
    for (auto &l : unreadableLines)
        Crypto::wipe(l);
    entries.clear();
    notes.clear();
    cards.clear();
    unreadableLines.clear();
}

bool FileVault::load()
{
    wipeData();
    skipped = 0;
    nextIdentifier = 1;
    ready = false;
    std::ifstream f(filename, std::ios::binary);
    if (!f)
    {
        ready = true; // no vault file yet: a new, empty vault
        return true;
    }
    std::string packed((std::istreambuf_iterator<char>(f)), {});
    if (packed.empty())
    {
        ready = true;
        return true;
    }
    std::string plain;
    try
    {
        plain = crypto->decrypt(packed);
    }
    catch (...)
    {
        return false;
    }
    std::istringstream in(plain);
    std::string line;
    bool first = true;
    while (std::getline(in, line))
    {
        const bool isFirst = first;
        first = false;
        if (line.empty())
            continue;
        auto p = Utils::splitEscaped(line);
        if (isFirst && p.size() == 1 && p[0] == "SV2")
            continue;
        bool ok = false;
        try
        {
            if (p[0] == "P" && p.size() >= 12)
            {
                PasswordEntry e;
                e.id = std::stoi(p[1]);
                e.title = p[2];
                e.site = p[3];
                e.username = p[4];
                e.password = p[5];
                e.notes = p[6];
                e.category = p[7];
                e.favorite = p[8] == "1";
                e.createdAt = std::stoll(p[9]);
                e.updatedAt = std::stoll(p[10]);
                int hc = std::stoi(p[11]);
                size_t idx = 12;
                for (int i = 0; i < hc && idx + 1 < p.size(); ++i)
                {
                    e.history.push_back({p[idx], (std::time_t)std::stoll(p[idx + 1])});
                    idx += 2;
                }
                // Trailing optional field; old vaults default to "last modified".
                e.passwordChangedAt = idx < p.size() && !p[idx].empty() ? (std::time_t)std::stoll(p[idx]) : e.updatedAt;
                entries.push_back(std::move(e));
                ok = true;
            }
            else if (p[0] == "N" && p.size() >= 8)
            {
                SecureNote n;
                n.id = std::stoi(p[1]);
                n.title = p[2];
                n.content = p[3];
                n.category = p[4];
                n.favorite = p[5] == "1";
                n.createdAt = std::stoll(p[6]);
                n.updatedAt = std::stoll(p[7]);
                notes.push_back(std::move(n));
                ok = true;
            }
            else if (p[0] == "C" && p.size() >= 9)
            {
                CardItem c;
                c.id = std::stoi(p[1]);
                c.title = p[2];
                c.cardholder = p[3];
                c.number = p[4];
                c.expiry = p[5];
                c.cvv = p[6];
                c.notes = p[7];
                c.favorite = p[8] == "1";
                c.createdAt = p.size() > 9 ? std::stoll(p[9]) : std::time(nullptr);
                c.updatedAt = p.size() > 10 ? std::stoll(p[10]) : c.createdAt;
                cards.push_back(std::move(c));
                ok = true;
            }
        }
        catch (...)
        {
            ok = false;
        }
        if (!ok)
        {
            ++skipped;
            unreadableLines.push_back(line);
        }
        for (auto &s : p)
            Crypto::wipe(s);
    }
    Crypto::wipe(plain);
    nextIdentifier = nextId();
    ready = true;
    return true;
}

bool FileVault::save()
{
    if (!ready)
        return false;
    std::ostringstream out;
    out << "SV2\n";
    for (const auto &e : entries)
    {
        std::vector<std::string> p = {"P", std::to_string(e.id), e.title, e.site, e.username, e.password, e.notes, e.category,
                                      flag(e.favorite), std::to_string(e.createdAt), std::to_string(e.updatedAt), std::to_string(e.history.size())};
        for (const auto &h : e.history)
        {
            p.push_back(h.password);
            p.push_back(std::to_string(h.changedAt));
        }
        p.push_back(std::to_string(e.passwordChangedAt ? e.passwordChangedAt : e.updatedAt)); // trailing optional field
        out << Utils::joinEscaped(p) << "\n";
    }
    for (const auto &n : notes)
        out << Utils::joinEscaped({"N", std::to_string(n.id), n.title, n.content, n.category, flag(n.favorite), std::to_string(n.createdAt), std::to_string(n.updatedAt)}) << "\n";
    for (const auto &c : cards)
        out << Utils::joinEscaped({"C", std::to_string(c.id), c.title, c.cardholder, c.number, c.expiry, c.cvv, c.notes, flag(c.favorite), std::to_string(c.createdAt), std::to_string(c.updatedAt)}) << "\n";
    for (const auto &l : unreadableLines)
        out << l << "\n"; // records this build could not read are kept byte-for-byte
    std::string plain = out.str();
    try
    {
        std::string packed = crypto->encrypt(plain);
        Crypto::wipe(plain);
        const std::string temp = filename + ".tmp";
        {
            std::ofstream f(temp, std::ios::trunc | std::ios::binary);
            if (!f)
                return false;
            f << packed;
            f.flush();
            if (!f)
            {
                f.close();
                std::error_code ec;
                fs::remove(temp, ec);
                return false;
            }
        }
        std::error_code ec;
        fs::rename(temp, filename, ec);
        if (ec)
        {
            fs::copy_file(temp, filename, fs::copy_options::overwrite_existing, ec);
            std::error_code ec2;
            fs::remove(temp, ec2);
            if (ec)
                return false;
        }
        return true;
    }
    catch (...)
    {
        Crypto::wipe(plain);
        return false;
    }
}

template <class F>
bool FileVault::commit(F &&undo)
{
    if (save())
        return true;
    undo();
    return false;
}

bool FileVault::addPassword(PasswordEntry e)
{
    if (!ready)
        return false;
    if (Utils::hasLineBreak(e.title) || Utils::hasLineBreak(e.site) || Utils::hasLineBreak(e.username) || Utils::hasLineBreak(e.category))
        return false;
    if (isDuplicateCredential(e.site, e.username))
        return false;
    const std::time_t now = std::time(nullptr);
    e.id = nextId();
    e.history.clear();
    if (!e.createdAt)
        e.createdAt = now;
    if (!e.updatedAt)
        e.updatedAt = e.createdAt;
    if (!e.passwordChangedAt)
        e.passwordChangedAt = e.createdAt;
    entries.push_back(std::move(e));
    return commit([&]
                  { entries.pop_back(); });
}

bool FileVault::updatePassword(const PasswordEntry &e)
{
    if (!ready)
        return false;
    auto p = findPassword(e.id);
    if (!p)
        return false;
    if (Utils::hasLineBreak(e.title) || Utils::hasLineBreak(e.site) || Utils::hasLineBreak(e.username) || Utils::hasLineBreak(e.category))
        return false;
    if (isDuplicateCredential(e.site, e.username, e.id))
        return false;
    PasswordEntry before = *p;
    const std::time_t now = std::time(nullptr);
    p->title = e.title;
    p->site = e.site;
    p->username = e.username;
    p->notes = e.notes;
    p->category = e.category;
    p->favorite = e.favorite;
    if (p->password != e.password)
    {
        p->history.insert(p->history.begin(), {before.password, before.passwordChangedAt ? before.passwordChangedAt : before.updatedAt});
        if (p->history.size() > HISTORY_LIMIT)
        {
            for (size_t i = HISTORY_LIMIT; i < p->history.size(); ++i)
                Crypto::wipe(p->history[i].password);
            p->history.resize(HISTORY_LIMIT);
        }
        p->password = e.password;
        p->passwordChangedAt = now;
    }
    p->updatedAt = now;
    const bool ok = commit([&]
                           { *p = before; });
    Crypto::wipe(before.password);
    Crypto::wipe(before.notes);
    return ok;
}

bool FileVault::deletePassword(int id)
{
    if (!ready)
        return false;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const auto &e)
                           { return e.id == id; });
    if (it == entries.end())
        return false;
    const size_t idx = (size_t)(it - entries.begin());
    PasswordEntry removed = std::move(*it);
    entries.erase(it);
    return commit([&]
                  { entries.insert(entries.begin() + (std::ptrdiff_t)idx, std::move(removed)); });
}

bool FileVault::addNote(SecureNote n)
{
    if (!ready)
        return false;
    if (Utils::hasLineBreak(n.title) || Utils::hasLineBreak(n.category))
        return false;
    n.id = nextId();
    if (!n.createdAt)
        n.createdAt = std::time(nullptr);
    if (!n.updatedAt)
        n.updatedAt = n.createdAt;
    notes.push_back(std::move(n));
    return commit([&]
                  { notes.pop_back(); });
}

bool FileVault::updateNote(const SecureNote &n)
{
    if (!ready)
        return false;
    auto p = findNote(n.id);
    if (!p)
        return false;
    if (Utils::hasLineBreak(n.title) || Utils::hasLineBreak(n.category))
        return false;
    SecureNote before = *p;
    p->title = n.title;
    p->content = n.content;
    p->category = n.category;
    p->favorite = n.favorite;
    p->updatedAt = std::time(nullptr);
    return commit([&]
                  { *p = before; });
}

bool FileVault::deleteNote(int id)
{
    if (!ready)
        return false;
    auto it = std::find_if(notes.begin(), notes.end(), [&](const auto &e)
                           { return e.id == id; });
    if (it == notes.end())
        return false;
    const size_t idx = (size_t)(it - notes.begin());
    SecureNote removed = std::move(*it);
    notes.erase(it);
    return commit([&]
                  { notes.insert(notes.begin() + (std::ptrdiff_t)idx, std::move(removed)); });
}

bool FileVault::addCard(CardItem c)
{
    if (!ready)
        return false;
    if (Utils::hasLineBreak(c.title) || Utils::hasLineBreak(c.cardholder) || Utils::hasLineBreak(c.expiry))
        return false;
    c.id = nextId();
    if (!c.createdAt)
        c.createdAt = std::time(nullptr);
    if (!c.updatedAt)
        c.updatedAt = c.createdAt;
    cards.push_back(std::move(c));
    return commit([&]
                  { cards.pop_back(); });
}

bool FileVault::updateCard(const CardItem &c)
{
    if (!ready)
        return false;
    auto p = findCard(c.id);
    if (!p)
        return false;
    if (Utils::hasLineBreak(c.title) || Utils::hasLineBreak(c.cardholder) || Utils::hasLineBreak(c.expiry))
        return false;
    CardItem before = *p;
    p->title = c.title;
    p->cardholder = c.cardholder;
    p->number = c.number;
    p->expiry = c.expiry;
    p->cvv = c.cvv;
    p->notes = c.notes;
    p->favorite = c.favorite;
    p->updatedAt = std::time(nullptr);
    return commit([&]
                  { *p = before; });
}

bool FileVault::deleteCard(int id)
{
    if (!ready)
        return false;
    auto it = std::find_if(cards.begin(), cards.end(), [&](const auto &e)
                           { return e.id == id; });
    if (it == cards.end())
        return false;
    const size_t idx = (size_t)(it - cards.begin());
    CardItem removed = std::move(*it);
    cards.erase(it);
    return commit([&]
                  { cards.insert(cards.begin() + (std::ptrdiff_t)idx, std::move(removed)); });
}

bool FileVault::backup(const std::string &dest) const
{
    try
    {
        if (!fs::exists(filename))
            return false;
        const fs::path d(dest);
        if (d.has_parent_path())
            fs::create_directories(d.parent_path());
        const std::string temp = dest + ".tmp";
        fs::copy_file(filename, temp, fs::copy_options::overwrite_existing);
        std::error_code ec;
        fs::rename(temp, d, ec);
        if (ec)
        {
            fs::copy_file(temp, d, fs::copy_options::overwrite_existing, ec);
            std::error_code ec2;
            fs::remove(temp, ec2);
            if (ec)
                return false;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool FileVault::restore(const std::string &source)
{
    const std::string temp = filename + ".restore.tmp";
    const std::string rollback = filename + ".restore.rollback";
    try
    {
        // Verify the backup decrypts with the current vault key before touching anything.
        std::ifstream in(source, std::ios::binary);
        if (!in)
            return false;
        std::string packed((std::istreambuf_iterator<char>(in)), {});
        if (packed.empty())
            return false;
        std::string probe = crypto->decrypt(packed);
        Crypto::wipe(probe);

        fs::copy_file(source, temp, fs::copy_options::overwrite_existing);
        if (fs::exists(filename))
            fs::copy_file(filename, rollback, fs::copy_options::overwrite_existing);

        std::error_code ec;
        fs::rename(temp, filename, ec);
        if (ec)
        {
            fs::copy_file(temp, filename, fs::copy_options::overwrite_existing, ec);
            fs::remove(temp);
            if (ec)
                throw std::runtime_error("Could not replace vault");
        }

        if (load())
        {
            std::error_code ec2;
            fs::remove(rollback, ec2);
            return true;
        }

        if (fs::exists(rollback))
            fs::copy_file(rollback, filename, fs::copy_options::overwrite_existing);
        std::error_code ec3;
        fs::remove(rollback, ec3);
        load();
        return false;
    }
    catch (...)
    {
        std::error_code ec;
        fs::remove(temp, ec);
        if (fs::exists(rollback, ec))
        {
            fs::copy_file(rollback, filename, fs::copy_options::overwrite_existing, ec);
            fs::remove(rollback, ec);
            load();
        }
        return false;
    }
}

bool FileVault::rekey(const std::vector<unsigned char> &newKey)
{
    if (!ready)
        return false;
    std::vector<unsigned char> oldKey = key;
    auto oldCrypto = crypto;
    key = newKey;
    crypto = std::make_shared<AesGcmEncryption>(key);
    if (save())
    {
        Crypto::wipe(oldKey);
        return true;
    }
    key = oldKey;
    crypto = oldCrypto;
    Crypto::wipe(oldKey);
    return false;
}
