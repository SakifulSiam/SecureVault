#!/usr/bin/env python3
"""CLI smoke script: register -> add -> generate -> score -> backup -> lock -> wrong password -> unlock -> quit.
Drives the real CLI through a pseudo-terminal (the CLI reads passwords with raw key input)."""
import os, pty, select, shutil, subprocess, sys, tempfile, time

exe = os.path.abspath(sys.argv[1])
data = tempfile.mkdtemp(prefix="sv_smoke_")
master, slave = pty.openpty()
proc = subprocess.Popen([exe, "--data-dir", data], stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)
buf = ""

def expect(text, timeout=30):
    global buf
    end = time.time() + timeout
    while text not in buf:
        left = end - time.time()
        if left <= 0:
            print("TIMEOUT waiting for %r\n--- output so far ---\n%s" % (text, buf)); proc.kill(); sys.exit(1)
        r, _, _ = select.select([master], [], [], min(left, 0.5))
        if r:
            try: buf += os.read(master, 4096).decode("utf-8", "replace")
            except OSError: break
    i = buf.index(text) + len(text); buf = buf[i:]

def send(s): os.write(master, (s + "\r").encode())

def step(prompt, answer): expect(prompt); send(answer)

PW = "Tr0ub4dor&3xKcd!"
expect("SecureVault CLI 3.0.0")
step("Choice: ", "1")
step("Username: ", "smoke"); step("Password: ", PW)
expect("User registered successfully.")
# add a credential with a generated password
step("Choice: ", "1")
step("Title: ", "Example"); step("Website/service: ", "example.com"); step("Username: ", "me"); step("Category [Other]: ", "")
step("Generate a password (y/n): ", "y"); step("Length (8-64): ", "16")
for q in ("uppercase", "lowercase", "digits", "symbols"): step("Include %s (y/n): " % q, "y")
expect("Generated: "); step("Notes (optional): ", ""); step("Favorite (y/n): ", "n")
expect("Credential added.")
# generator
step("Choice: ", "8"); step("Length (8-64): ", "20")
for q in ("Uppercase", "Lowercase", "Digits", "Symbols", "Avoid ambiguous characters"): step("%s (y/n): " % q, "y")
step("Copy to clipboard (y/n): ", "n")
# security score
step("Choice: ", "10"); expect("Security score    : 100/100")
# backup to the default location
step("Choice: ", "14"); step("Backup file path: ", ""); expect("Encrypted backup created.")
# lock, wrong password, unlock
step("Choice: ", "17")
step("Username: ", "smoke"); step("Password: ", "not-the-password"); expect("Invalid credentials."); expect("Authentication failed.")
step("Username: ", "smoke"); step("Password: ", PW)
expect("===== SECUREVAULT =====")
# the credential is still there after the lock/unlock cycle
step("Choice: ", "2"); expect("Example | example.com")
# activity log shows the events
step("Choice: ", "13"); expect("LOCK_VAULT"); 
step("Choice: ", "18")
proc.wait(timeout=15)
assert proc.returncode == 0, "CLI exited with %s" % proc.returncode
log = open(os.path.join(data, "smoke_audit.log")).read()
for code in ("LOGIN_SUCCESS", "ADD_ENTRY", "BACKUP", "LOCK_VAULT", "LOGIN_FAILED", "VAULT_UNLOCK", "LOGOUT"):
    assert code in log, "missing %s in activity log" % code
shutil.rmtree(data, ignore_errors=True)
print("cli smoke: OK")
