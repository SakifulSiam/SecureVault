#pragma once
#include "AuthService.h"
#include "PasswordChecker.h"
#include "PasswordGenerator.h"
#include "Utils.h"
#include "VaultAnalyzer.h"
#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <memory>

// List model of SUMMARIES. It never holds a password, only derived facts (§4.5.1).
class CredentialsModel : public QAbstractListModel
{
    Q_OBJECT
    struct Row { int id; QString title, site, username, strength; bool weak, reused, old, fav; };
    QVector<Row> rows;
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, SiteRole, UsernameRole, StrengthRole, WeakRole, ReusedRole, OldRole, FavRole };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : rows.size(); }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || i.row() >= rows.size()) return {};
        const Row &r = rows[i.row()];
        switch (role) { case IdRole: return r.id; case TitleRole: return r.title; case SiteRole: return r.site;
            case UsernameRole: return r.username; case StrengthRole: return r.strength; case WeakRole: return r.weak;
            case ReusedRole: return r.reused; case OldRole: return r.old; case FavRole: return r.fav; }
        return {};
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{IdRole, "id"}, {TitleRole, "title"}, {SiteRole, "site"}, {UsernameRole, "username"}, {StrengthRole, "strength"},
                {WeakRole, "isWeak"}, {ReusedRole, "isReused"}, {OldRole, "isOld"}, {FavRole, "favorite"}};
    }
    void clear() { beginResetModel(); rows.clear(); endResetModel(); }
    void rebuild(Session &s, const QString &query) {
        FileVault &v = s.vault();
        std::vector<int> ids;
        if (query.trimmed().isEmpty()) for (const auto &e : v.getPasswords()) ids.push_back(e.id);
        else ids = v.searchPasswordIds(query.trimmed().toStdString()); // ids only (P1-4)
        HealthReport rep = VaultAnalyzer::analyze(v);
        QVector<Row> out;
        for (int id : ids) {
            PasswordEntry *e = v.findPassword(id);
            if (!e) continue;
            Row r{id, QString::fromStdString(e->title), QString::fromStdString(e->site), QString::fromStdString(e->username),
                  QString::fromStdString(PasswordChecker().assess(e->password).displayLabel()), false, false, false, e->favorite};
            for (const auto &f : rep.credentials) if (f.id == id) { r.weak = f.weak; r.reused = f.reused; r.old = f.old; }
            out.push_back(r);
        }
        std::sort(out.begin(), out.end(), [](const Row &a, const Row &b) { return a.title.toLower() < b.title.toLower(); });
        beginResetModel(); rows = out; endResetModel();
    }
};

// State machine (§4.4): firstRun | locked | unlocked | unreadable. Only AuthService can reach "unlocked".
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString lockReason READ lockReason NOTIFY stateChanged)
    Q_PROPERTY(QString username READ username NOTIFY stateChanged)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(int score READ score NOTIFY healthChanged)
    Q_PROPERTY(QString band READ band NOTIFY healthChanged)
    Q_PROPERTY(int attention READ attention NOTIFY healthChanged)
    Q_PROPERTY(int weakCount READ weakCount NOTIFY healthChanged)
    Q_PROPERTY(int reusedCount READ reusedCount NOTIFY healthChanged)
    Q_PROPERTY(int oldCount READ oldCount NOTIFY healthChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY motionChanged)
    Q_PROPERTY(QString motionMode READ motionMode NOTIFY motionChanged)
    Q_PROPERTY(bool discarded READ discarded NOTIFY stateChanged)
    Q_PROPERTY(int rev READ rev NOTIFY revChanged)
    Q_PROPERTY(int total READ total NOTIFY healthChanged)
    Q_PROPERTY(int totalNotes READ totalNotes NOTIFY healthChanged)
    Q_PROPERTY(int totalCards READ totalCards NOTIFY healthChanged)
    Q_PROPERTY(int totalFavorites READ totalFavorites NOTIFY healthChanged)
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY countdownChanged)
    Q_PROPERTY(int kdfIterations READ kdfIterations NOTIFY stateChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QObject *credentials READ credentials CONSTANT)
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;
    QString state() const { return state_; }
    bool busy() const { return busy_; }
    QString error() const { return error_; }
    QString lockReason() const { return lockReason_; }
    QString username() const { return username_; }
    QString dataDir() const { return QString::fromStdString(Utils::dataDir().string()); }
    int score() const { return rep_.score; }
    QString band() const { return QString::fromStdString(rep_.band); }
    int attention() const { return rep_.attentionCount; }
    int total() const { return rep_.total; }
    int totalNotes() const;
    int totalCards() const;
    int totalFavorites() const;
    int rev() const { return rev_; }
    QString motionMode() const;                 // "system" | "reduce" | "full" (UI preference only)
    bool reduceMotion() const;
    bool discarded() const { return discarded_; }
    Q_INVOKABLE QVariantMap windowGeometry() const;   // UI-only QSettings
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int w, int h);
    Q_INVOKABLE void setMotionMode(const QString &mode);
    Q_INVOKABLE void draftOpen(bool open) { drafts_ += open ? 1 : -1; if (drafts_ < 0) drafts_ = 0; }
    int progress() const { return progress_; }
    int weakCount() const { return rep_.weak; }
    int reusedCount() const { return rep_.reused; }
    int oldCount() const { return rep_.old; }
    int secondsLeft() const { return secondsLeft_; }
    QObject *credentials() { return &model_; }
    int kdfIterations() const { return session_ ? (int)session_->kdfIterations() : 0; }
    QString version() const { return QStringLiteral(SV_VERSION); }
    Q_INVOKABLE bool backupTo(const QUrl &file);                          // plain copy of the encrypted vault, written via a temp file
    Q_INVOKABLE void restoreFrom(const QUrl &file, const QString &masterPassword); // needs the current master password

    Q_INVOKABLE void signIn(const QString &user, const QString &password) { authenticate(user, password, false); }
    Q_INVOKABLE void createAccount(const QString &user, const QString &password, bool acknowledged);
    Q_INVOKABLE void lock() { lockNow("LOCK_VAULT", "Manual lock", tr("You locked the vault.")); }
    Q_INVOKABLE void clearError() { setError({}); }
    Q_INVOKABLE void stayUnlocked() { if (session_) session_->security().touch(); }
    Q_INVOKABLE QVariantMap policy(const QString &password) const; // live checklist, from the real policy
    Q_INVOKABLE void search(const QString &q) { query_ = q; if (session_) model_.rebuild(*session_, q); }
    Q_INVOKABLE bool copyPassword(int id);   // copying happens in C++; QML gets success or failure only
    Q_INVOKABLE bool copyUsername(int id);
    Q_INVOKABLE void setFavorite(int id, bool on);
    // Credential workflow. Secrets cross to QML only for an explicit reveal or generator output the user asked for.
    Q_INVOKABLE QVariantMap detail(int id) const;                 // non-secret fields only
    Q_INVOKABLE QVariantList passwordHistory(int id) const;
    Q_INVOKABLE QString revealPassword(int id);                   // logs REVEAL_PASSWORD; QML clears it after 15 s
    Q_INVOKABLE bool saveCredential(const QVariantMap &draft);    // id 0 adds; otherwise updates; "password" empty = keep stored
    Q_INVOKABLE bool removeCredential(int id);
    Q_INVOKABLE QVariantMap generate(int length, bool upper, bool lower, bool digits, bool symbols, bool avoidAmbiguous);
    Q_INVOKABLE QVariantMap generatePassphrase(int words);
    Q_INVOKABLE bool copyGenerated();   // copies the last generator output in C++
    // Secure notes (content is secret: it leaves C++ only when the user opens a note to read or edit it)
    Q_INVOKABLE QVariantList notes(const QString &query) const;   // summaries; bind with `app.rev` to refresh
    Q_INVOKABLE QVariantMap noteDetail(int id) const;
    Q_INVOKABLE bool saveNote(const QVariantMap &draft);
    Q_INVOKABLE bool removeNote(int id);
    // Cards: number and CVV stay in C++ until an explicit reveal or copy (both logged)
    Q_INVOKABLE QVariantList cards(const QString &query) const;
    Q_INVOKABLE QVariantMap cardDetail(int id) const;
    Q_INVOKABLE QString revealCardField(int id, const QString &field);
    Q_INVOKABLE bool copyCardField(int id, const QString &field);
    Q_INVOKABLE bool saveCard(const QVariantMap &draft);
    Q_INVOKABLE bool removeCard(int id);
    Q_INVOKABLE void changeMasterPassword(const QString &current, const QString &next, bool acknowledged);
    Q_INVOKABLE QVariantList favorites() const;                           // passwords, notes and cards, titles only
    Q_INVOKABLE void setFavoriteOf(const QString &kind, int id, bool on); // kind: password | note | card
    Q_INVOKABLE QVariantList paletteItems(const QString &query) const;    // command palette: titles only, never secrets
    Q_INVOKABLE QVariantList activity() const;                    // newest first, at most 300 events
    Q_INVOKABLE QVariantMap assess(const QString &password) const;
    bool eventFilter(QObject *o, QEvent *e) override;
signals:
    void motionChanged();
    void progressChanged();
    void revChanged();
    void notice(const QString &text);
    void stateChanged(); void busyChanged(); void errorChanged(); void healthChanged(); void countdownChanged();
private:
    void authenticate(const QString &user, const QString &password, bool registering);
    void finish(int result);
    void adopt(); void lockNow(const char *code, const char *detail, const QString &reason);
    void setState(const QString &s) { state_ = s; emit stateChanged(); }
    void setError(const QString &e) { error_ = e; emit errorChanged(); }
    void setBusy(bool b) { busy_ = b; emit busyChanged(); }
    QString state_, error_, lockReason_, username_, query_;
    bool busy_ = false, registering_ = false;
    std::unique_ptr<Session> session_;
    std::shared_ptr<std::unique_ptr<Session>> pending_;
    QFutureWatcher<bool> verifyWatcher_;
    QFutureWatcher<int> changeWatcher_;
    int progress_ = -1, drafts_ = 0;
    bool discarded_ = false;
    std::string restorePath_;
    QFutureWatcher<int> watcher_;   // key derivation runs off the UI thread (§4.6)
    QTimer idle_;
    HealthReport rep_;
    std::string lastGen_;
    int secondsLeft_ = -1, rev_ = 0;
    CredentialsModel model_;
};
