# Phase 0 report: making the backend GUI-safe

## Exit criteria (§3.5)

| Criterion | Result |
|---|---|
| `backend_regression_tests.cpp` | 24/24 checks pass (the supplied file has 24 `CHECK`s, including the `SV_HAS_PASSWORD_CHANGED_AT` one) |
| Rebuild with `-fsanitize=address,undefined` | `ctest` under `-DSV_SANITIZE=ON`: all three tests pass, no ASan/UBSan findings, leak detection on |
| `-Wall -Wextra` | no warnings (core, CLI, both test programs) |
| CLI smoke script | `tests/cli_smoke.py` passes (register, add, generate, score, backup, lock, wrong password, unlock, quit), driven through a real pty |
| Core has no `std::cin` / `std::cout` | verified by grep; `securevault_core` also includes no Qt |

Also added: `tests/core_tests.cpp`, 83 checks (fault injection, escaping fuzz in both directions against a copy of the
original encoder/decoder, malformed records, analyzer, auth flows, a legacy `users.db`, instance lock, and more).

**Not verified (no environment here):** Windows (CNG path, `ClipboardManager`, `InstanceLock`, executable-path lookup,
the `users.db` replace-by-remove path) and macOS were never compiled. The Windows clipboard code is written to the
Win32 documentation but has not run. Qt was not touched; apt here offers Qt 6.4, below the required 6.5.

## P0

**P0-1 Multi-line text.** `joinEscaped` writes LF and CR as `\n` and `\r`; `splitEscaped` decodes them. Fuzzed 20,000
random records in each direction against a copy of the original encoder/decoder: zero mismatches. `load()` no longer
skips bad records silently: it counts them (`FileVault::skippedRecords()`) and keeps each unreadable line verbatim, writing
it back on every save, so nothing is lost even if a later build understands it. Header `SV2` is recognised explicitly.
README documents that a vault saved by this build must not be opened with an old CLI.
*Files:* `Utils.cpp/.h`, `FileVault.cpp/.h`, `README.md`.

**P0-2 Failed load then write.** `FileVault::ready` is set only by a successful `load()` or `restore()`; every mutator and
`save()` refuse otherwise. `AuthService::login` returns `VaultUnreadable` and still hands back the session so the GUI can
offer restore.
*Files:* `FileVault.cpp/.h`, `AuthService.cpp`.

**P0-3 Trusting the caller / phantom data.** `update*` keep stored `id`, `createdAt` and password `history` and copy only
editable fields. Every mutator is transactional (change, `save()`, undo on failure). Fault-injection test: a directory
sits where the temp file goes, so every save fails; memory and file stay unchanged for add, update, delete and `rekey`.
*Files:* `FileVault.cpp/.h`, `tests/core_tests.cpp`.

**P0-4 "Old" means recently edited.** `PasswordEntry::passwordChangedAt`: set at creation and whenever the password
string changes; trailing optional field in the `P` record (defaults to `updatedAt` for old vaults); used by the analyzer
and as the history timestamp. `PasswordEntry.h` defines `SV_HAS_PASSWORD_CHANGED_AT` itself.
*Files:* `PasswordEntry.h`, `FileVault.cpp`, `VaultAnalyzer.cpp`.

**P0-5 Uniqueness.** Reject only when the website is non-empty and website and username both match, case-insensitive
(`Vault::isDuplicateCredential`).
*Files:* `Vault.cpp/.h`, `FileVault.cpp`.

**P0-6 Data folder.** `Utils::dataDir()`: `setDataDir`/`--data-dir`, then `SECUREVAULT_DATA_DIR`, then
`<exe folder>/data`, then a per-user folder when the exe folder is not writable. Cached; `setDataDir({})` clears.
`Utils::legacyWorkingDirData()` finds an old `./data`; the CLI asks before using it and never copies. The GUI's Settings
page should show `Utils::dataDir()`.
*Files:* `Utils.cpp/.h`, `cli/main.cpp`.

**P0-7 Services.** New static library `securevault_core`. `AuthService` (register, login, verifyMasterPassword,
changeMasterPassword with progress callback, kdfIterations, plus `anyUserExists`/`userExists`/`isValidUsername`),
`Session` (owns vault key, `FileVault`, `AuditLogger`, `SecurityManager`; `lock()` saves, logs, destroys the vault, wipes
keys, clears the clipboard if still ours), `VaultAnalyzer::analyze` (original formula, per-credential flags, reuse groups
as ids, `attentionCount`, band, `hasData`). Unknown user and wrong password both return `InvalidCredentials` after one
PBKDF2 run. `User` is deleted; the master password is not retained after derivation (`Session` keeps only the vault key,
salt and verifier). The CLI is now `cli/main.cpp` + `cli/Console.*`; menu and wording are unchanged.
*Files:* `AuthService.*`, `Session.*`, `VaultAnalyzer.*`, `cli/*`, `CMakeLists.txt`; removed `User.*`, `main.cpp`.

**P0-8 Passphrases.** 2048-word BIP-39 English list (`WordList.h`, MIT with the BIPs repo), 11 bits per word, default 5
words, uniform selection by rejection sampling, `wordListSize()`. The suite observes all 2048 words. Do not offer
passphrases as master passwords until the GUI work shows N × 11 bits honestly.
*Files:* `PasswordGenerator.*`, `WordList.h`.

**P0-9 Master-password policy.** `MasterPasswordPolicy::evaluate`: length ≥ 12 and `!assess().weak`; used by
registration, change and the future UI checklist. `Password1234!`, `Qwerty123456!`, `Summer2024!!!!` are rejected;
`Tr0ub4dor&3xKcd!` is accepted.
*Files:* `PasswordChecker.*`, `AuthService.cpp`.

**P0-10 Label vs flag.** `PasswordAssessment::displayLabel()` and `weakReason` ("Common pattern" / "Too simple").
A test over 12 samples proves the label is "Weak" exactly when the flag is set.
*Files:* `PasswordChecker.*`, `cli/main.cpp` (strength menu uses it).

## P1

**P1-1 `rekey`.** Re-encrypts the in-memory state, saves, restores the old key on failure. The second file parser is gone.
**P1-2 Backups.** Bare file names work; written to `<dest>.tmp`, then renamed. Registration now saves an empty vault
immediately so a backup always has a file to copy.
**P1-3 Audit.** Added LOGIN_FAILED (existing users only), REVEAL_PASSWORD, REVEAL_CARD; DELETE_NOTE/DELETE_CARD log the
title; COPY_PASSWORD only on success. **UPDATE_NOTE, UPDATE_CARD, COPY_CARD_FIELD are not emitted yet**: the CLI has no
operation that does those things, so the GUI controllers must write them (`session.audit().log(...)`).
**P1-4 Search/favourites.** `searchPasswordIds`, `searchNoteIds`, `searchCardIds` (never number or CVV),
`favoritePasswordIds/NoteIds/CardIds`, `categories()`. All return ids.
**P1-5 Clipboard (Windows).** `CF_UNICODETEXT` via UTF-8→UTF-16, history/cloud exclusion formats, `copyWithAutoClear`,
and `clear()` now clears only when `GetClipboardSequenceNumber()` still matches ours. Untested (see above).
**P1-6 `SecurityManager`.** Constructor takes the auto-lock seconds; debug-only `SECUREVAULT_AUTOLOCK_SECONDS`;
`idleSeconds()`; cooldown (none for 2 failures, then 5, 10, 30, 60 s) via `lockoutRemaining()`. Timestamps are atomic
because the CLI timer thread reads them while the main thread writes (a data race in the original).
**P1-7 Version.** CMake `project(VERSION 3.0.0)` → `SV_VERSION`; `Version.h` has a fallback for plain `g++` builds.
**P1-8 `unb64`.** Accumulator masked to 24 bits; UBSan clean.
**P1-9 Single instance.** `InstanceLock` (flock / exclusive-share `CreateFile`) on `<data>/.securevault.lock`; the CLI
prints "SecureVault is already open for this data folder."
**P1-10 `poolSize(options)`** added.
**P1-11 Warnings.** `-Wall -Wextra` clean; the unused `pause()` is gone and `read()`'s result is handled.

## Decisions I made without asking

- The CLI's "Change master password" now asks for the **current** master password first, because
  `changeMasterPassword(session, current, new)` requires it (§6 rule 9).
- `FileVault` is non-copyable and wipes its key and secret strings on destruction (`Crypto::wipe`, `secureZero`).
- Single-line fields (title, website, username, category, cardholder, expiry) reject line breaks inside the vault (§6.10).
- New audit code: none beyond P1-3. Detail text for LOGIN_SUCCESS/VAULT_UNLOCK changed slightly ("Unlocked with master password").
- Test file: the supplied suite is unchanged except `fresh()` calls `Utils::setDataDir`, as its own note asked.

## Defects found and not fixed (outside §3)

- Usernames that differ only by case (`Siam`, `siam`) are different accounts but share one vault file on Windows and macOS.
- `users.db` registration appends in place (not atomic); the Windows replace path removes the file before renaming (P2).
- Every failed sign-in with a real username appends a LOGIN_FAILED line; no rotation yet (P2).
- Unknown-user timing is equalised against failed logins only; an account created with a different iteration count still
  differs from the 600,000-iteration dummy.
- `std::string` secrets cannot be wiped reliably; wiping is best effort, as the brief says.
