#pragma once
#include <string>
#include <vector>
#include <ctime>

#define SV_HAS_PASSWORD_CHANGED_AT 1
struct PasswordHistoryItem
{
    std::string password;
    std::time_t changedAt{}; // when this (previous) password was set
};

struct PasswordEntry
{
    int id = 0;
    std::string title;
    std::string site;
    std::string username;
    std::string password;
    std::string notes;
    std::string category = "Other";
    bool favorite = false;
    std::time_t createdAt{};
    std::time_t updatedAt{};          // last modified (any field)
    std::time_t passwordChangedAt{};  // last time the password string itself changed (0 = unset: add() fills it)
    std::vector<PasswordHistoryItem> history;
};

struct SecureNote
{
    int id = 0;
    std::string title;
    std::string content;
    std::string category = "Notes";
    bool favorite = false;
    std::time_t createdAt{};
    std::time_t updatedAt{};
};

struct CardItem
{
    int id = 0;
    std::string title;
    std::string cardholder;
    std::string number;
    std::string expiry;
    std::string cvv;
    std::string notes;
    bool favorite = false;
    std::time_t createdAt{};
    std::time_t updatedAt{};
};
