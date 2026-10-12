#pragma once
#include "PasswordEntry.h"
#include "EncryptionEngine.h"
#include <memory>
#include <string>
#include <vector>
class Vault
{
protected:
    std::vector<PasswordEntry> entries;
    std::vector<SecureNote> notes;
    std::vector<CardItem> cards;
    std::shared_ptr<EncryptionEngine> crypto;
public:
    virtual ~Vault() = default;
    virtual bool save() = 0;
    virtual bool load() = 0;
    virtual bool addPassword(PasswordEntry entry) = 0;
    virtual bool updatePassword(const PasswordEntry &entry) = 0;
    virtual bool deletePassword(int id) = 0;
    virtual bool addNote(SecureNote note) = 0;
    virtual bool updateNote(const SecureNote &note) = 0;
    virtual bool deleteNote(int id) = 0;
    virtual bool addCard(CardItem card) = 0;
    virtual bool updateCard(const CardItem &card) = 0;
    virtual bool deleteCard(int id) = 0;
    const std::vector<PasswordEntry> &getPasswords() const { return entries; }
    const std::vector<SecureNote> &getNotes() const { return notes; }
    const std::vector<CardItem> &getCards() const { return cards; }
    bool doesSiteExist(const std::string &name);
    // True when another credential (id != excludeId) has the same non-empty website AND username
    // (case-insensitive). Credentials without a website never collide.
    bool isDuplicateCredential(const std::string &site, const std::string &username, int excludeId = 0) const;
    PasswordEntry *findPassword(int id);
    SecureNote *findNote(int id);
    CardItem *findCard(int id);
    // Legacy: returns copies of whole entries, secrets included. GUI code must use the id variants.
    std::vector<PasswordEntry> searchPasswords(const std::string &query) const;
    std::vector<PasswordEntry> favorites() const;
    // Id-returning variants (P1-4): no extra copies of secrets. ASCII case-insensitive substring.
    std::vector<int> searchPasswordIds(const std::string &query) const; // title, site, username, category, notes
    std::vector<int> searchNoteIds(const std::string &query) const;     // title, category, content
    std::vector<int> searchCardIds(const std::string &query) const;     // title, cardholder, notes (never number/CVV)
    std::vector<int> favoritePasswordIds() const;
    std::vector<int> favoriteNoteIds() const;
    std::vector<int> favoriteCardIds() const;
    // Distinct password and note categories, sorted, case-insensitive de-duplication.
    std::vector<std::string> categories() const;
    int nextId() const;
};
