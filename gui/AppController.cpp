#include "AppController.h"
#include "ClipboardManager.h"
#include "CardBrand.h"
#include "Crypto.h"
#include <QDateTime>
#include <QSettings>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QPointer>
#include <QEvent>
#include <QKeyEvent>
#include <cmath>
#include <QtConcurrent>

AppController::AppController(QObject *parent) : QObject(parent)
{
    state_ = AuthService::anyUserExists() ? "locked" : "firstRun";   // Locked is the default when a user exists
    connect(&watcher_, &QFutureWatcher<int>::finished, this, [this] { finish(watcher_.result()); });
    connect(&verifyWatcher_, &QFutureWatcher<bool>::finished, this, [this] {
        setBusy(false);
        if (!session_) return;
        if (!verifyWatcher_.result()) { emit notice(tr("Master password is wrong. Nothing was changed.")); return; }
        if (session_->vault().restore(restorePath_)) {
            session_->audit().log("RESTORE", "Vault restored from backup");
            model_.rebuild(*session_, query_); rep_ = VaultAnalyzer::analyze(session_->vault()); emit healthChanged();
            emit notice(tr("Vault restored."));
        } else emit notice(tr("That file couldn't be restored. SecureVault kept your current vault."));
    });
    connect(&changeWatcher_, &QFutureWatcher<int>::finished, this, [this] {
        setBusy(false); progress_ = -1; emit progressChanged();
        switch ((ChangeResult)changeWatcher_.result()) {
        case ChangeResult::Ok: emit notice(tr("Master password changed. Backups made before now can't be restored.")); emit healthChanged(); break;
        case ChangeResult::WrongPassword: setError(tr("Your current master password is wrong. Nothing was changed.")); break;
        case ChangeResult::WeakPassword: setError(tr("The new master password is too easy to guess. Nothing was changed.")); break;
        case ChangeResult::VaultNotReady: setError(tr("The vault isn't readable, so the password can't be changed.")); break;
        case ChangeResult::StorageError: setError(tr("Couldn't write the new password. Your previous password is still in force.")); break;
        }
    });
    idle_.setInterval(1000);
    connect(&idle_, &QTimer::timeout, this, [this] {
        if (!session_ || busy_) return;
        int left = session_->security().autoLockSeconds() - session_->security().idleSeconds();
        int shown = left <= 60 ? std::max(left, 0) : -1;           // quiet countdown in the last 60 s
        if (shown != secondsLeft_) { secondsLeft_ = shown; emit countdownChanged(); }
        if (left <= 0) lockNow("AUTO_LOCK", "Auto-lock after inactivity", tr("Locked after 5 minutes without activity."));
    });
}
AppController::~AppController() { if (session_) session_->lock("LOGOUT", "Window closed"); }

QVariantMap AppController::policy(const QString &pw) const
{
    auto p = MasterPasswordPolicy::evaluate(pw.toStdString());
    return {{"lengthOk", p.lengthOk}, {"notWeak", p.notWeak}, {"ok", p.ok}};
}

void AppController::createAccount(const QString &user, const QString &pw, bool ack)
{
    if (!ack) { setError(tr("Confirm that you understand a forgotten master password cannot be recovered.")); return; }
    authenticate(user, pw, true);
}

void AppController::authenticate(const QString &user, const QString &pw, bool registering)
{
    if (busy_) return;
    setBusy(true); setError({});
    registering_ = registering;
    if (state_ != "locked" || username_.isEmpty()) username_ = user.trimmed();
    pending_ = std::make_shared<std::unique_ptr<Session>>();
    auto holder = pending_;
    std::string u = user.trimmed().toStdString(), p = pw.toStdString();
    LoginPurpose purpose = state_ == "locked" && session_ == nullptr && !lockReason_.isEmpty() ? LoginPurpose::Unlock : LoginPurpose::SignIn;
    watcher_.setFuture(QtConcurrent::run([=]() mutable -> int {
        int r = registering ? (int)AuthService::registerUser(u, p, *holder)
                            : (int)AuthService::login(u, p, *holder, {}, purpose);
        Crypto::wipe(p); // best effort; the master password is not kept after derivation
        return r;
    }));
}

void AppController::finish(int r)
{
    setBusy(false);
    if (registering_) {
        switch ((RegisterResult)r) {
        case RegisterResult::Ok: adopt(); return;
        case RegisterResult::InvalidUsername: setError(tr("Use 3 to 32 letters, digits, underscores or hyphens.")); break;
        case RegisterResult::UsernameTaken: setError(tr("That username is taken.")); break;
        case RegisterResult::WeakPassword: setError(tr("That master password is too easy to guess.")); break;
        case RegisterResult::StorageError: setError(tr("SecureVault couldn't write to its data folder.")); break;
        }
    } else {
        switch ((LoginResult)r) {
        case LoginResult::Ok: adopt(); return;
        case LoginResult::VaultUnreadable: adopt(); return;   // session is returned, but the vault refuses all writes
        case LoginResult::InvalidCredentials: setError(tr("Username or master password is wrong.")); break;
        case LoginResult::StorageError: setError(tr("SecureVault couldn't read its data folder.")); break;
        }
    }
    pending_.reset();
}

void AppController::adopt()
{
    session_ = std::move(*pending_); pending_.reset();
    username_ = QString::fromStdString(session_->username());
    lockReason_.clear(); discarded_ = false; secondsLeft_ = -1;
    if (!session_->vault().isReady()) { setState("unreadable"); return; }   // never writes (P0-2)
    model_.rebuild(*session_, query_);
    rep_ = VaultAnalyzer::analyze(session_->vault());
    idle_.start();
    setState("unlocked"); emit healthChanged(); emit countdownChanged();
}

// Order from §4.7: show lock view first (state change unloads the shell), then destroy data.
void AppController::lockNow(const char *code, const char *detail, const QString &reason)
{
    idle_.stop();
    discarded_ = drafts_ > 0; drafts_ = 0;   // an editor was open: its unsaved text is discarded with the lock
    lockReason_ = reason; query_.clear(); secondsLeft_ = -1;
    setState("locked");
    model_.clear();
    rep_ = HealthReport{};
    Crypto::wipe(lastGen_);
    if (session_) { session_->lock(code, detail); session_.reset(); }
    emit healthChanged(); emit countdownChanged();
}

bool AppController::copyPassword(int id)
{
    if (!session_) return false;
    PasswordEntry *e = session_->vault().findPassword(id);
    if (!e) return false;
    bool ok = ClipboardManager::copyWithAutoClear(e->password, 30);
    if (ok) session_->audit().log("COPY_PASSWORD", e->title);   // logged only on success (P1-3)
    return ok;
}

void AppController::setFavorite(int id, bool on)
{
    if (!session_) return;
    PasswordEntry *e = session_->vault().findPassword(id);
    if (!e) return;
    PasswordEntry draft = *e; draft.favorite = on;
    if (session_->vault().updatePassword(draft)) { model_.rebuild(*session_, query_); ++rev_; emit revChanged(); }   // stored dates/history are kept by the core
}

bool AppController::eventFilter(QObject *o, QEvent *e)
{
    switch (e->type()) {
    case QEvent::KeyPress: {
        auto *k = static_cast<QKeyEvent *>(e);
        // Ctrl+L locks from anywhere, including while a modal form is open (Qt blocks window Shortcuts then).
        if (session_ && !k->isAutoRepeat() && k->key() == Qt::Key_L && (k->modifiers() & Qt::ControlModifier)) {
            lock(); return true;
        }
        if (session_) session_->security().touch();
        break;
    }
    case QEvent::MouseButtonPress: case QEvent::MouseMove: case QEvent::TouchBegin:
        if (session_) session_->security().touch(); break;
    default: break;
    }
    return QObject::eventFilter(o, e);
}

// ---------------- credentials ----------------
static bool singleLine(const QString &s) { return !s.contains('\n') && !s.contains('\r'); }

QVariantMap AppController::detail(int id) const
{
    if (!session_) return {};
    const PasswordEntry *e = session_->vault().findPassword(id);
    if (!e) return {};
    return {{"id", e->id}, {"title", QString::fromStdString(e->title)}, {"site", QString::fromStdString(e->site)},
            {"username", QString::fromStdString(e->username)}, {"notes", QString::fromStdString(e->notes)},
            {"category", QString::fromStdString(e->category)},
            {"changed", QDateTime::fromSecsSinceEpoch(e->passwordChangedAt).toString("d MMM yyyy")},
            {"historyCount", (int)e->history.size()}};
}

QString AppController::revealPassword(int id)
{
    if (!session_) return {};
    PasswordEntry *e = session_->vault().findPassword(id);
    if (!e) return {};
    session_->audit().log("REVEAL_PASSWORD", e->title);
    return QString::fromStdString(e->password);
}

bool AppController::saveCredential(const QVariantMap &d)
{
    setError({});
    if (!session_) return false;
    const QString title = d["title"].toString().trimmed(), site = d["site"].toString().trimmed(),
                  user = d["username"].toString().trimmed(), cat = d["category"].toString().trimmed(), pw = d["password"].toString();
    const int id = d["id"].toInt();
    if (title.isEmpty()) { setError(tr("Add a title.")); return false; }
    if (!singleLine(title) || !singleLine(site) || !singleLine(user) || !singleLine(cat)) { setError(tr("Title, website, username and category can't contain line breaks.")); return false; }
    if (id == 0 && pw.isEmpty()) { setError(tr("Add a password.")); return false; }
    FileVault &v = session_->vault();
    if (v.isDuplicateCredential(site.toStdString(), user.toStdString(), id)) { setError(tr("You already have a credential for this website and username.")); return false; }
    bool ok;
    if (id == 0) {
        PasswordEntry e; e.title = title.toStdString(); e.site = site.toStdString(); e.username = user.toStdString();
        e.password = pw.toStdString(); e.notes = d["notes"].toString().toStdString();
        e.category = cat.isEmpty() ? "Other" : cat.toStdString();
        e.createdAt = e.updatedAt = e.passwordChangedAt = std::time(nullptr);
        ok = v.addPassword(e);
        if (ok) session_->audit().log("ADD_ENTRY", e.title);
    } else {
        PasswordEntry *cur = v.findPassword(id);
        if (!cur) { setError(tr("That password no longer exists.")); return false; }
        PasswordEntry e = *cur;   // stored password is kept unless a new one was typed
        e.title = title.toStdString(); e.site = site.toStdString(); e.username = user.toStdString();
        e.notes = d["notes"].toString().toStdString(); e.category = cat.isEmpty() ? "Other" : cat.toStdString();
        if (!pw.isEmpty()) e.password = pw.toStdString();
        e.updatedAt = std::time(nullptr);
        ok = v.updatePassword(e);   // core keeps id, createdAt and history, and stamps passwordChangedAt itself
        if (ok) session_->audit().log("UPDATE_ENTRY", e.title);
    }
    if (!ok) { setError(tr("Couldn't save. Your vault was not changed.")); return false; }
    model_.rebuild(*session_, query_); rep_ = VaultAnalyzer::analyze(v); emit healthChanged();
    return true;
}

bool AppController::removeCredential(int id)
{
    if (!session_) return false;
    FileVault &v = session_->vault();
    PasswordEntry *e = v.findPassword(id);
    if (!e) return false;
    const std::string title = e->title;
    if (!v.deletePassword(id)) { setError(tr("Couldn't delete. Your vault was not changed.")); return false; }
    session_->audit().log("DELETE_ENTRY", title);
    model_.rebuild(*session_, query_); rep_ = VaultAnalyzer::analyze(v); emit healthChanged();
    return true;
}

QVariantMap AppController::generate(int len, bool up, bool lo, bool di, bool sy, bool amb)
{
    PasswordGeneratorOptions o; o.length = len; o.upper = up; o.lower = lo; o.digits = di; o.symbols = sy; o.avoidAmbiguous = amb;
    PasswordGenerator g; std::string s = g.generate(o);
    Crypto::wipe(lastGen_); lastGen_ = s;
    int pool = PasswordGenerator::poolSize(o);
    if (s.empty() || pool <= 0) return {{"ok", false}};
    return {{"ok", true}, {"text", QString::fromStdString(s)}, {"bits", int(len * std::log2((double)pool))}};   // exact for random output
}

QVariantMap AppController::generatePassphrase(int words)
{
    words = std::clamp(words, 3, 12);
    std::string s = PasswordGenerator().generatePassphrase(words);
    if (s.empty()) return {{"ok", false}};
    Crypto::wipe(lastGen_); lastGen_ = s;
    return {{"ok", true}, {"text", QString::fromStdString(s)}, {"bits", int(words * std::log2((double)PasswordGenerator::wordListSize()))}};
}

bool AppController::copyGenerated()
{
    return !lastGen_.empty() && ClipboardManager::copyWithAutoClear(lastGen_, 30);
}

QVariantMap AppController::assess(const QString &pw) const
{
    auto a = PasswordChecker().assess(pw.toStdString());
    return {{"label", QString::fromStdString(a.displayLabel())}, {"reason", QString::fromStdString(a.weakReason)}, {"score", a.score}};
}

// ---------------- backup and restore ----------------
bool AppController::backupTo(const QUrl &file)
{
    if (!session_ || busy_) return false;
    const std::string path = file.toLocalFile().toStdString();
    bool ok = !path.empty() && session_->vault().backup(path);
    if (ok) session_->audit().log("BACKUP", "Backup written");
    emit notice(ok ? tr("Backup saved.") : tr("Couldn't save the backup."));
    return ok;
}

void AppController::restoreFrom(const QUrl &file, const QString &pw)
{
    if (!session_ || busy_) return;
    restorePath_ = file.toLocalFile().toStdString();
    setBusy(true);
    Session *s = session_.get();            // vault stays untouched by the worker; only the key check runs there
    std::string p = pw.toStdString();
    verifyWatcher_.setFuture(QtConcurrent::run([s, p]() mutable { bool ok = AuthService::verifyMasterPassword(*s, p); Crypto::wipe(p); return ok; }));
}

// ---------------- secure notes and activity log ----------------
QVariantList AppController::notes(const QString &query) const
{
    QVariantList out;
    if (!session_) return out;
    FileVault &v = session_->vault();
    std::vector<int> ids;
    if (query.trimmed().isEmpty()) for (const auto &n : v.getNotes()) ids.push_back(n.id);
    else ids = v.searchNoteIds(query.trimmed().toStdString());
    for (int id : ids)
        if (const SecureNote *n = v.findNote(id))
            out.push_back(QVariantMap{{"id", n->id}, {"title", QString::fromStdString(n->title)}, {"category", QString::fromStdString(n->category)}, {"favorite", n->favorite}});
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) { return a.toMap()["title"].toString().toLower() < b.toMap()["title"].toString().toLower(); });
    return out;
}

QVariantMap AppController::noteDetail(int id) const
{
    if (!session_) return {};
    const SecureNote *n = session_->vault().findNote(id);
    if (!n) return {};
    return {{"id", n->id}, {"title", QString::fromStdString(n->title)}, {"category", QString::fromStdString(n->category)}, {"content", QString::fromStdString(n->content)}};
}

bool AppController::saveNote(const QVariantMap &d)
{
    setError({});
    if (!session_) return false;
    const QString title = d["title"].toString().trimmed(), cat = d["category"].toString().trimmed();
    const int id = d["id"].toInt();
    if (title.isEmpty()) { setError(tr("Add a title.")); return false; }
    if (!singleLine(title) || !singleLine(cat)) { setError(tr("Title and category can't contain line breaks.")); return false; }
    FileVault &v = session_->vault();
    bool ok;
    if (id == 0) {
        SecureNote n; n.title = title.toStdString(); n.content = d["content"].toString().toStdString();
        n.category = cat.isEmpty() ? "Notes" : cat.toStdString(); n.createdAt = n.updatedAt = std::time(nullptr);
        ok = v.addNote(n);
        if (ok) session_->audit().log("ADD_NOTE", n.title);
    } else {
        SecureNote *cur = v.findNote(id);
        if (!cur) { setError(tr("That note no longer exists.")); return false; }
        SecureNote n = *cur;   // core keeps id and createdAt
        n.title = title.toStdString(); n.content = d["content"].toString().toStdString();
        n.category = cat.isEmpty() ? "Notes" : cat.toStdString(); n.updatedAt = std::time(nullptr);
        ok = v.updateNote(n);
        if (ok) session_->audit().log("UPDATE_NOTE", n.title);
    }
    if (!ok) { setError(tr("Couldn't save. Your vault was not changed.")); return false; }
    ++rev_; emit revChanged();
    return true;
}

bool AppController::removeNote(int id)
{
    if (!session_) return false;
    const SecureNote *n = session_->vault().findNote(id);
    if (!n) return false;
    const std::string title = n->title;
    if (!session_->vault().deleteNote(id)) { setError(tr("Couldn't delete. Your vault was not changed.")); return false; }
    session_->audit().log("DELETE_NOTE", title);
    ++rev_; emit revChanged();
    return true;
}

QVariantList AppController::activity() const
{
    QVariantList out;
    if (!session_) return out;
    const auto &ev = session_->audit().getEvents();
    for (auto it = ev.rbegin(); it != ev.rend() && out.size() < 300; ++it)
        out.push_back(QVariantMap{{"time", QDateTime::fromSecsSinceEpoch(it->timestamp).toString("d MMM yyyy, HH:mm")},
                                  {"action", QString::fromStdString(it->action)}, {"detail", QString::fromStdString(it->detail)}});
    return out;
}

// ---------------- cards ----------------
static QString digitsOnly(const QString &s) { QString o; for (QChar c : s) if (c.isDigit()) o += c; return o; }

QVariantList AppController::cards(const QString &query) const
{
    QVariantList out;
    if (!session_) return out;
    FileVault &v = session_->vault();
    std::vector<int> ids;
    if (query.trimmed().isEmpty()) for (const auto &c : v.getCards()) ids.push_back(c.id);
    else ids = v.searchCardIds(query.trimmed().toStdString());   // never matches number or CVV
    for (int id : ids)
        if (const CardItem *c = v.findCard(id))
            out.push_back(QVariantMap{{"id", c->id}, {"title", QString::fromStdString(c->title)}, {"cardholder", QString::fromStdString(c->cardholder)},
                {"expiry", QString::fromStdString(c->expiry)}, {"brand", QString::fromStdString(CardBrand::fromNumber(c->number))},
                {"last4", QString::fromStdString(CardBrand::last4(c->number))}, {"favorite", c->favorite}});
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) { return a.toMap()["title"].toString().toLower() < b.toMap()["title"].toString().toLower(); });
    return out;
}

QVariantMap AppController::cardDetail(int id) const
{
    if (!session_) return {};
    const CardItem *c = session_->vault().findCard(id);
    if (!c) return {};
    return {{"title", QString::fromStdString(c->title)}, {"cardholder", QString::fromStdString(c->cardholder)},
            {"expiry", QString::fromStdString(c->expiry)}, {"notes", QString::fromStdString(c->notes)}};
}

QString AppController::revealCardField(int id, const QString &field)
{
    if (!session_) return {};
    const CardItem *c = session_->vault().findCard(id);
    if (!c) return {};
    session_->audit().log("REVEAL_CARD", c->title + (field == "cvv" ? ": security code" : ": card number"));
    return QString::fromStdString(field == "cvv" ? c->cvv : c->number);
}

bool AppController::copyCardField(int id, const QString &field)
{
    if (!session_) return false;
    const CardItem *c = session_->vault().findCard(id);
    if (!c) return false;
    bool ok = ClipboardManager::copyWithAutoClear(field == "cvv" ? c->cvv : c->number, 30);
    if (ok) session_->audit().log("COPY_CARD_FIELD", c->title + (field == "cvv" ? ": security code" : ": card number"));
    return ok;
}

bool AppController::saveCard(const QVariantMap &d)
{
    setError({});
    if (!session_) return false;
    const QString title = d["title"].toString().trimmed(), holder = d["cardholder"].toString().trimmed(), exp = d["expiry"].toString().trimmed();
    const QString num = digitsOnly(d["number"].toString()), cvv = d["cvv"].toString().trimmed();
    const int id = d["id"].toInt();
    if (title.isEmpty()) { setError(tr("Add a title.")); return false; }
    if (!singleLine(title) || !singleLine(holder) || !singleLine(exp)) { setError(tr("Title, cardholder and expiry can't contain line breaks.")); return false; }
    if ((id == 0 || !num.isEmpty()) && (num.size() < 12 || num.size() > 19)) { setError(tr("A card number has 12 to 19 digits.")); return false; }
    if (!cvv.isEmpty() && (digitsOnly(cvv) != cvv || cvv.size() < 3 || cvv.size() > 4)) { setError(tr("A security code has 3 or 4 digits.")); return false; }
    FileVault &v = session_->vault();
    bool ok;
    if (id == 0) {
        CardItem c; c.title = title.toStdString(); c.cardholder = holder.toStdString(); c.expiry = exp.toStdString();
        c.number = num.toStdString(); c.cvv = cvv.toStdString(); c.notes = d["notes"].toString().toStdString();
        c.createdAt = c.updatedAt = std::time(nullptr);
        ok = v.addCard(c);
        if (ok) session_->audit().log("ADD_CARD", c.title);
    } else {
        CardItem *cur = v.findCard(id);
        if (!cur) { setError(tr("That card no longer exists.")); return false; }
        CardItem c = *cur;   // number and CVV are kept unless new ones were typed
        c.title = title.toStdString(); c.cardholder = holder.toStdString(); c.expiry = exp.toStdString(); c.notes = d["notes"].toString().toStdString();
        if (!num.isEmpty()) c.number = num.toStdString();
        if (!cvv.isEmpty()) c.cvv = cvv.toStdString();
        c.updatedAt = std::time(nullptr);
        ok = v.updateCard(c);
        if (ok) session_->audit().log("UPDATE_CARD", c.title);
    }
    if (!ok) { setError(tr("Couldn't save. Your vault was not changed.")); return false; }
    ++rev_; emit revChanged();
    return true;
}

bool AppController::removeCard(int id)
{
    if (!session_) return false;
    const CardItem *c = session_->vault().findCard(id);
    if (!c) return false;
    const std::string title = c->title;
    if (!session_->vault().deleteCard(id)) { setError(tr("Couldn't delete. Your vault was not changed.")); return false; }
    session_->audit().log("DELETE_CARD", title);
    ++rev_; emit revChanged();
    return true;
}

// ---------------- master password ----------------
void AppController::changeMasterPassword(const QString &cur, const QString &next, bool ack)
{
    if (!session_ || busy_) return;
    setError({});
    if (!ack) { setError(tr("Confirm that you understand a forgotten master password cannot be recovered.")); return; }
    setBusy(true); progress_ = 0; emit progressChanged();
    Session *s = session_.get();   // the UI is locked out while busy, so only this worker touches the vault
    std::string a = cur.toStdString(), b = next.toStdString();
    QPointer<AppController> self(this);
    changeWatcher_.setFuture(QtConcurrent::run([s, a, b, self]() mutable -> int {
        ProgressFn fn = [self](int done, int total) {
            if (self && total > 0) QMetaObject::invokeMethod(self, [self, done, total] { if (self) { self->progress_ = done * 100 / total; emit self->progressChanged(); } }, Qt::QueuedConnection);
        };
        int r = (int)AuthService::changeMasterPassword(*s, a, b, fn);   // verifies, applies the policy, re-encrypts, logs
        Crypto::wipe(a); Crypto::wipe(b);
        return r;
    }));
}

// ---------------- favourites and command palette ----------------
QVariantList AppController::favorites() const
{
    QVariantList out;
    if (!session_) return out;
    FileVault &v = session_->vault();
    for (int id : v.favoritePasswordIds()) if (const PasswordEntry *e = v.findPassword(id))
        out.push_back(QVariantMap{{"kind", "password"}, {"id", id}, {"title", QString::fromStdString(e->title)}, {"hint", QString::fromStdString(e->username)}});
    for (int id : v.favoriteNoteIds()) if (const SecureNote *n = v.findNote(id))
        out.push_back(QVariantMap{{"kind", "note"}, {"id", id}, {"title", QString::fromStdString(n->title)}, {"hint", QString::fromStdString(n->category)}});
    for (int id : v.favoriteCardIds()) if (const CardItem *c = v.findCard(id))
        out.push_back(QVariantMap{{"kind", "card"}, {"id", id}, {"title", QString::fromStdString(c->title)}, {"hint", QString::fromStdString("•••• " + CardBrand::last4(c->number))}});
    return out;
}

void AppController::setFavoriteOf(const QString &kind, int id, bool on)
{
    if (!session_) return;
    FileVault &v = session_->vault();
    if (kind == "password") { setFavorite(id, on); return; }
    bool ok = false;
    if (kind == "note") { if (SecureNote *n = v.findNote(id)) { SecureNote d = *n; d.favorite = on; ok = v.updateNote(d); } }
    else if (kind == "card") { if (CardItem *c = v.findCard(id)) { CardItem d = *c; d.favorite = on; ok = v.updateCard(d); } }
    if (ok) { ++rev_; emit revChanged(); }
}

QVariantList AppController::paletteItems(const QString &query) const
{
    QVariantList out;
    const QString q = query.trimmed();
    if (!session_ || q.isEmpty()) return out;
    FileVault &v = session_->vault();
    const std::string s = q.toStdString();
    int n = 0;
    for (int id : v.searchPasswordIds(s)) if (n++ < 6) if (const PasswordEntry *e = v.findPassword(id))
        out.push_back(QVariantMap{{"kind", "password"}, {"id", id}, {"label", QString::fromStdString(e->title)}, {"hint", "Password"}});
    n = 0;
    for (int id : v.searchNoteIds(s)) if (n++ < 4) if (const SecureNote *x = v.findNote(id))
        out.push_back(QVariantMap{{"kind", "note"}, {"id", id}, {"label", QString::fromStdString(x->title)}, {"hint", "Secure note"}});
    n = 0;
    for (int id : v.searchCardIds(s)) if (n++ < 4) if (const CardItem *c = v.findCard(id))
        out.push_back(QVariantMap{{"kind", "card"}, {"id", id}, {"label", QString::fromStdString(c->title)}, {"hint", "Card"}});
    return out;
}

// ---------------- motion preference (UI-only QSettings, never secrets) ----------------
QString AppController::motionMode() const { return QSettings().value("ui/motion", "system").toString(); }
bool AppController::reduceMotion() const
{
    const QString m = motionMode();
    if (m == "reduce") return true;
    if (m == "full") return false;
#ifdef Q_OS_WIN
    BOOL on = TRUE;   // Windows "Show animations" setting
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0)) return !on;
#endif
    return false;
}
void AppController::setMotionMode(const QString &mode)
{
    if (mode != "system" && mode != "reduce" && mode != "full") return;
    QSettings().setValue("ui/motion", mode); emit motionChanged();
}

QVariantMap AppController::windowGeometry() const
{
    QSettings s;
    if (!s.contains("ui/w")) return {};
    return {{"x", s.value("ui/x").toInt()}, {"y", s.value("ui/y").toInt()}, {"w", s.value("ui/w").toInt()}, {"h", s.value("ui/h").toInt()}};
}
void AppController::saveWindowGeometry(int x, int y, int w, int h)
{
    QSettings s; s.setValue("ui/x", x); s.setValue("ui/y", y); s.setValue("ui/w", w); s.setValue("ui/h", h);
}
