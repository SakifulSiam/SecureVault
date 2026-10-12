# 🛡️ SecureVault

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B)](https://isocpp.org/)
[![Qt 6](https://img.shields.io/badge/Qt-6.5+-41CD52?logo=qt)](https://www.qt.io/)
[![CMake](https://img.shields.io/badge/CMake-3.16+-064F8C?logo=cmake)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)]()
[![Security](https://img.shields.io/badge/Crypto-AES--256--GCM%20%2B%20PBKDF2-blueviolet)]()

**SecureVault** is an enterprise-grade, local, zero-knowledge password and credentials manager built with modern C++17 and Qt 6. It features a responsive, dark-mode desktop GUI (Qt Quick / QML) alongside a lightning-fast terminal CLI, both powered by a shared, headless core cryptographic engine (`securevault_core`).

All data is encrypted end-to-end using authenticated **AES-256-GCM** with keys derived via **PBKDF2-HMAC-SHA256** (600,000 iterations). Your master password is never stored on disk or transmitted anywhere. Everything remains strictly under your control on your local machine.

---

## ✨ Features & Capabilities

- **Dual Interfaces**:
  - 🖥️ **Desktop GUI**: Built with Qt 6 Quick / QML with custom dark styling, real-time health meters, quick action cards, and keyboard navigation (`Ctrl+K` command palette, `Ctrl+L` instant lock).
  - 💻 **Terminal CLI**: Fast, distraction-free command-line interface with masked input and terminal controls.
- **Strong Cryptography**:
  - Authenticated symmetric encryption: **AES-256-GCM** with fresh cryptographic random nonces for every write.
  - Key derivation: **PBKDF2-HMAC-SHA256** with **600,000 iterations** and domain-separated salts.
  - Native Windows CNG (`bcrypt`) on Windows; OpenSSL 3.x on Linux/macOS.
  - Memory wiping (`SecureZeroMemory` / `OPENSSL_cleanse`) on secret destruction.
- **Complete Credential Management**:
  - Passwords with category tags, favorites, and password history tracking.
  - **Secure Notes**: Encrypted multi-line notes for sensitive text.
  - **Payment Cards**: Cardholder, number, expiry, CVV with automatic card brand detection (Visa, Mastercard, Amex, Discover).
- **Security Center & Vault Health**:
  - Live vault security score (0–100) and health grade.
  - Identification of weak passwords, reused credentials, and old credentials (>180 days).
- **Cryptographic Password & Passphrase Generator**:
  - Character generator: Configurable length (8–128), character sets (uppercase, lowercase, numbers, symbols, ambiguous filter).
  - Passphrase generator: 3–12 random words from the BIP-39 English word list.
  - Real-time entropy calculation (bits) and strength evaluation.
- **Privacy & Safety Safeguards**:
  - **Clipboard Auto-Clear**: Automatically wipes copied secrets from the system clipboard after 30 seconds.
  - **Inactivity Auto-Lock**: Vault auto-locks after 5 minutes of inactivity with a quiet 60-second countdown warning.
  - **Atomic Persistence**: Vault writes use atomic temporary files with automatic rollback on failure.
  - **Encrypted Backups**: Export and restore encrypted backups with integrity verification.
  - **Audit Logger**: Detailed security activity log with timestamps (passwords are never logged).

---

## 🔒 Security Architecture

| Mechanism | Implementation | Details |
|---|---|---|
| **Vault Encryption** | AES-256-GCM | Authenticated encryption with 12-byte random nonce and 16-byte authentication tag per save |
| **Key Derivation** | PBKDF2-HMAC-SHA256 | 600,000 rounds with domain-separated salts for authentication and vault decryption |
| **Zero-Knowledge** | Local Key Derivation | Master password is never written to disk; only SHA-256 verifier is stored |
| **Memory Security** | RAII & Secure Cleansing | Sensitive vectors/strings wiped on session destruction or vault lock |
| **Integrity & Concurrency** | Single-Writer File Lock | Inter-process locking prevents simultaneous writes from corrupting data |
| **Rate Limiting** | Authentication Backoff | Progressive delays after consecutive invalid login attempts |

---

## 📁 Project Structure

```
SecureVault/
├── CMakeLists.txt          # Root CMake configuration
├── README.md               # Project documentation
├── .gitignore              # Git ignore rules
│
├── include/                # Core C++ headers (Qt-free, console-free)
│   ├── AuthService.h       # User registration, authentication, key derivation
│   ├── Crypto.h            # AES-256-GCM & PBKDF2 cryptographic wrappers
│   ├── EncryptionEngine.h  # Abstract encryption interface
│   ├── FileVault.h         # Persisted vault implementation with atomic saves
│   ├── Vault.h             # In-memory vault data model
│   ├── PasswordEntry.h     # Password, Note, and Card data structures
│   ├── PasswordChecker.h   # Password strength & vulnerability analyzer
│   ├── PasswordGenerator.h # CSPRNG password & BIP-39 passphrase generator
│   ├── VaultAnalyzer.h     # Vault-wide security scoring & audit heuristics
│   ├── InstanceLock.h      # Single-instance file lock
│   ├── Session.h           # Active authenticated session state
│   ├── SecurityManager.h   # Idle timeout & activity tracking
│   ├── ClipboardManager.h  # Clipboard copy with timed auto-clear
│   └── Utils.h             # Path resolution, string sanitization, encoding
│
├── src/                    # Core library implementation (libsecurevault_core)
│   ├── AuthService.cpp
│   ├── Crypto.cpp
│   ├── FileVault.cpp
│   ├── Vault.cpp
│   ├── PasswordChecker.cpp
│   ├── PasswordGenerator.cpp
│   ├── VaultAnalyzer.cpp
│   ├── InstanceLock.cpp
│   ├── Session.cpp
│   ├── SecurityManager.cpp
│   ├── ClipboardManager.cpp
│   └── Utils.cpp
│
├── gui/                    # Qt 6 Quick / QML Desktop GUI
│   ├── CMakeLists.txt
│   ├── main.cpp            # GUI application entry point
│   ├── AppController.h/cpp # QML-C++ controller interface
│   ├── CardBrand.h         # Offline payment card brand classifier
│   ├── qml/
│   │   ├── Main.qml        # Main window, pages, and dialogs
│   │   └── Theme.qml       # Theme color palette, typography, tokens
│   ├── fonts/              # Bundled fonts (Inter, JetBrains Mono)
│   ├── icons/              # SVG UI icons
│   └── securevault.rc      # Windows application resource & icon
│
├── cli/                    # Terminal CLI
│   ├── main.cpp            # Interactive terminal application
│   ├── Console.h/cpp       # Masked input, console utilities
│   └── conio_cross.h       # Cross-platform terminal keystroke reader
│
└── tests/                  # Unit and regression test suites
    ├── backend_regression_tests.cpp
    ├── core_tests.cpp
    └── cli_smoke.py
```

---

## 🛠️ Building & Running

### Prerequisites
- **C++17** compatible compiler (GCC 9+, Clang 10+, or MSVC 2019+)
- **CMake 3.16+**
- *(Optional for Desktop GUI)* **Qt 6.5+** with `Quick`, `QuickControls2`, and `Concurrent` modules. If Qt is not present, the build automatically compiles the terminal CLI.

### Option A: Windows (MinGW / MSYS2)
```powershell
# Configure build
cmake -G "MinGW Makefiles" -B build -S .

# Compile GUI, CLI, and tests
cmake --build build -j4

# Run Desktop GUI
.\build\gui\SecureVaultGui.exe

# Or run Terminal CLI
.\build\SecureVault.exe
```

### Option B: Windows (Visual Studio)
```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\SecureVault.exe
```

### Option C: Linux / macOS
```bash
# Ubuntu / Debian prerequisites:
sudo apt update
sudo apt install build-essential cmake libssl-dev qt6-base-dev qt6-declarative-dev

# Configure and build
cmake -S . -B build
cmake --build build -j4

# Run
./build/gui/SecureVaultGui    # Desktop GUI
./build/SecureVault           # Terminal CLI
```

---

## 🧪 Running Tests

The test suite validates cryptographic operations, fault-tolerant persistence, atomicity, and edge-case handling:

```bash
ctest --test-dir build --output-on-failure
```

Tests include:
- `backend_regression`: Verification of core invariants and persistence compliance.
- `core_tests`: 83+ comprehensive unit tests covering AES-256-GCM, PBKDF2 derivations, fault injection, escaping fuzzing, and transaction rollbacks.
- `cardbrand`: Offline credit card brand classification verification.

---

## 📄 License & Credits

- **BIP-39 English Word List**: MIT License.
- **Inter Font**: SIL Open Font License 1.1.
- **JetBrains Mono Font**: SIL Open Font License 1.1.
- **Lucide Icons**: ISC License.
- **Qt 6**: LGPLv3 (dynamically linked).
