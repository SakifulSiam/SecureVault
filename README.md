# SecureVault 3.0.0

A local password manager. The terminal app (`SecureVault`) is a thin front-end over `securevault_core`, a Qt-free, console-free library that a desktop GUI can use directly.

## Security model

- Master passwords are never stored directly.
- PBKDF2-HMAC-SHA256 derives separate authentication and vault keys using domain-separated salts.
- New registrations and master-password changes use 600,000 PBKDF2 iterations; existing accounts retain their stored iteration count for compatibility.
- Vault data is encrypted as a whole with AES-256-GCM and authenticated on decryption.
- Every vault encryption uses a fresh cryptographically secure random nonce.
- Authentication verification uses a constant-time comparison.
- Passwords, secure notes, cards, and password history are stored only inside the encrypted vault.
- The password generator uses the operating system / OpenSSL cryptographic RNG rather than `mt19937`.
- Usernames are restricted to safe filename characters, preventing path traversal through per-user storage paths.
- Login attempts are capped and failed attempts include a small delay to slow rapid guessing.
- Backup restore validates the encrypted backup before replacing the active vault. If a restore fails, SecureVault puts the previous vault back.
- Vault writes are written safely using a temporary file. A failed save never leaves a changed item in memory.
- A vault that could not be read is never overwritten: every write is refused until a load or restore succeeds.
- The old XOR cipher is no longer used.

On Windows, cryptographic operations use Windows CNG (`bcrypt`) and therefore do not require OpenSSL. On Linux/macOS, CMake uses OpenSSL 3.x for the cryptographic backend.

## Features

- User registration and login with masked terminal input
- Five-attempt authentication protection with failed-attempt delay
- AES-256-GCM encrypted vault
- PBKDF2-HMAC-SHA256 key derivation
- Add, view, edit, delete credentials
- Search credentials
- Categories and favorites
- Password history (last five previous passwords)
- Password strength score
- Password reuse detection
- Password age monitoring
- Security Center / vault health score
- Secure notes
- Secure payment-card records
- Cryptographically secure password generator
- Clipboard copy on Windows
- Encrypted backup and validated restore (a failed restore puts the previous vault back)
- Master-password rotation with vault re-encryption
- Security audit log that never records passwords
- Manual lock
- Five-minute inactivity auto-lock
- Clipboard copy clears itself after 30 seconds when it still holds the copied text (Windows only)
- Passphrase generator: 5 random words from a 2048-word list by default (about 11 bits per word)

## Build on Windows with Visual Studio

Open the project folder in Visual Studio/CMake and build `SecureVault` as usual. The Windows build uses the system `bcrypt` library.

Command line alternative:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\SecureVault.exe
```

## Build on Linux

Install OpenSSL development headers, then:

```bash
cmake -S . -B build
cmake --build build
./build/SecureVault
```

## Tests

```bash
cmake -S . -B build -DSV_SANITIZE=ON     # AddressSanitizer + UndefinedBehaviorSanitizer
cmake --build build
ctest --test-dir build --output-on-failure
```

`backend_regression` is the Phase 0 acceptance suite, `core_tests` covers the services, and `cli_smoke` drives the real
CLI through a pseudo-terminal (not run on Windows).

## Data directory

The data folder is resolved in this order:

1. `--data-dir <folder>` (CLI option) or the `SECUREVAULT_DATA_DIR` environment variable
2. `<folder of the executable>/data`
3. a per-user application-data folder, if the executable's folder is not writable

If an older `./data` folder with a `users.db` exists in the current directory but not at the resolved path, the CLI asks
whether to use it. Nothing is copied silently. Only one SecureVault instance can use a data folder at a time.

```text
data/
  users.db
  <username>_vault.sv
  <username>_audit.log
  backups/
```

Do not commit real vaults, user databases, audit logs, or backups to source control.

## Compatibility with older builds

Vaults, `users.db` and activity logs written by 2.x still open. **A vault saved by this version must not be opened with an
older CLI binary:** line breaks inside notes are now stored as `\n` and `\r`, and each credential has a trailing
password-changed time that the old build would ignore. Records this version cannot read are counted, reported, and kept
byte-for-byte in the file.

A backup made before a master-password change cannot be restored afterwards (the old salt is gone).

## Important compatibility note

The original project used plaintext-ish records and XOR encryption. SecureVault 2.x intentionally does **not** silently import those records because doing so could create ambiguous or unsafe security assumptions. Start a fresh vault and manually migrate entries if needed.

## OOP concepts demonstrated

- Abstraction: `Vault`, `EncryptionEngine`
- Inheritance: `FileVault : Vault`, `AesGcmEncryption : EncryptionEngine`
- Polymorphism through virtual vault/crypto interfaces
- Encapsulation of authentication and vault persistence
- Composition through `Vault` containing credential/note/card collections
- RAII and smart pointers
- STL containers and algorithms
- Exception-safe file/crypto boundaries
