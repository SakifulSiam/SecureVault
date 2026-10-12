#pragma once
#include "Vault.h"
#include <string>
#include <vector>

class FileVault : public Vault
{
    std::string username;
    std::string filename;
    std::string backupDir;
    std::vector<unsigned char> key;
    int nextIdentifier = 1;
    bool ready = false;                       // true only after load() or restore() succeeded
    int skipped = 0;                          // records load() could not understand
    std::vector<std::string> unreadableLines; // kept verbatim and written back, so nothing is lost

    template <class F>
    bool commit(F &&undo);
    void wipeData();

public:
    FileVault(std::string username, std::vector<unsigned char> encryptionKey);
    ~FileVault() override;
    FileVault(const FileVault &) = delete;
    FileVault &operator=(const FileVault &) = delete;

    // Until load() or restore() succeeds, every mutator and save() refuses and writes nothing (P0-2).
    bool isReady() const { return ready; }
    // Number of records the last load() could not read (P0-1). They are preserved on save.
    int skippedRecords() const { return skipped; }

    bool save() override;
    bool load() override;
    // Mutators are transactional: change, save(), undo the change if saving fails (P0-3).
    // update*() keep the stored id and createdAt (and a password's history); only editable fields apply.
    bool addPassword(PasswordEntry entry) override;
    bool updatePassword(const PasswordEntry &entry) override;
    bool deletePassword(int id) override;
    bool addNote(SecureNote note) override;
    bool updateNote(const SecureNote &note) override;
    bool deleteNote(int id) override;
    bool addCard(CardItem card) override;
    bool updateCard(const CardItem &card) override;
    bool deleteCard(int id) override;
    bool backup(const std::string &destination) const;
    bool restore(const std::string &source);
    // Re-encrypts the in-memory state under newKey and saves; restores the old key if saving fails.
    bool rekey(const std::vector<unsigned char> &newKey);
    const std::string &getFilename() const { return filename; }
};
