#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <functional>

// What the library knows about one book file without opening it for reading.
struct BookInfo {
    QString id;
    QString digest;  // The whole SHA-256 of the file, of which id is the start.
    QString path;
    QString format;  // "epub" or "pdf"
    QString title;
    QString author;
    QString language;
    QString coverSource;  // A file inside the unpacked EPUB, if it has one.
    int pages = 0;        // PDFs only; an EPUB's pages depend on the window.
};

// What's on disk where a store (library.json, a book's notes) should be.
// Only Missing and Valid may be written: a file Nell can't parse, or one a
// newer Nell wrote, is read as far as it can be and never overwritten.
enum class StoreState { Missing, Valid, Corrupt, Newer };

struct Store {
    StoreState state = StoreState::Missing;
    QJsonObject object;
    QString problem;  // Why it can't be written, for the reader.

    bool writable() const { return state == StoreState::Missing || state == StoreState::Valid; }
};

// The books the reader has added, kept in ~/.local/share/nell/library.json,
// with each book's place, bookmarks and highlights beside it in books/<id>.json.
// Books are referenced where they live and never modified; a book's id is a
// hash of its contents, so moving or re-adding the file finds its notes again.
class Library : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList books READ books NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // Stores Nell found it can't safely write (a damaged or newer library,
    // book notes, or settings), one sentence each. They stay as they are on
    // disk and the app works on without saving to them. storageWarning
    // fires when one is added after startup; read this at startup.
    Q_PROPERTY(QStringList storageProblems READ storageProblems NOTIFY storageProblemsChanged)

public:
    static constexpr int FormatVersion = 1;
    // How long a save waits for another Nell to finish its own, in ms.
    static constexpr int LockTimeout = 5000;

    explicit Library(const QString &dataDirectory = {}, const QString &cacheDirectory = {}, QObject *parent = nullptr);

    QVariantList books() const { return m_books; }
    QString error() const { return m_error; }
    QStringList storageProblems() const { return m_storageProblems; }
    // Records a problem once, however often it's reported.
    void reportStorageProblem(const QString &problem);

    // Adds a file or file:// URL, or finds it if it's already there, and
    // returns its id; an empty id means it failed and error says why.
    Q_INVOKABLE QString add(const QString &pathOrUrl);
    Q_INVOKABLE int addUrls(const QVariantList &urls);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE QVariantMap book(const QString &id) const;
    Q_INVOKABLE QVariantList sorted(const QString &sort, const QString &filter) const;
    Q_INVOKABLE void clearError();

    void touch(const QString &id, double progress);
    // Whether the book's file still holds what was added: its size and time
    // are compared, and the file hashed again only if they changed. Returns
    // the id of what's there now, which differs from id when the contents
    // changed, or an empty id when it can't be read. With record, a file
    // that's only been touched has its new size and time saved.
    QString checkFile(const QString &id, bool record);
    // The file of oldId was changed into newId: each entry names the other.
    void markRevision(const QString &oldId, const QString &newId);
    QString findId(const QString &pathOrId) const;

    QString cacheRoot() const { return m_cacheDirectory; }
    QString cacheDirectory(const QString &id) const { return m_cacheDirectory + "/" + id; }
    // A book's notes as stored. Unknown fields are kept, so writing back
    // what was read loses nothing a newer Nell added.
    Store annotations(const QString &id) const;
    // Saves a book's notes, merged with any another Nell saved since base
    // was read, and hands back what was written. Refuses, and says why in
    // error, when the file on disk can't be read safely.
    bool saveAnnotations(const QString &id, const QJsonObject &annotations, const QJsonObject &base,
                         QJsonObject *saved = nullptr);
    static QJsonObject mergeAnnotations(const QJsonObject &base, const QJsonObject &ours, const QJsonObject &theirs);

    // Nell was called Quire before 0.3.0. Its data, settings, and cache
    // directories under these roots move across, unless Nell already has its
    // own, and the covers the library points into the cache follow. Each
    // step is checked and repeated on the next run if it didn't happen, so a
    // run that stopped half way is finished later. Returns what went wrong.
    // Call it only when Nell is about to use its storage for real: a window,
    // or a command that writes.
    static QStringList adoptQuire(const QString &dataRoot, const QString &configRoot, const QString &cacheRoot);
    // Whether Quire's library is still waiting to be moved across.
    static bool quireWaiting(const QString &dataRoot);

    static QString fileDigest(const QString &path);
    static QString fileId(const QString &path) { return fileDigest(path).left(16); }
    static bool inspect(const QString &path, const QString &cacheDirectory, BookInfo *info, QString *error);
    static bool writeJson(const QString &path, const QJsonObject &object);
    static Store readStore(const QString &path);

signals:
    void changed();
    void errorChanged();
    void storageProblemsChanged();
    void storageWarning(const QString &problem);

private:
    void load();
    bool update(const std::function<void(QVariantList &books)> &change);
    void setError(const QString &message);
    int indexOf(const QString &id) const;
    QString makeCover(const BookInfo &info);

    QString m_dataDirectory;
    QString m_cacheDirectory;
    QVariantList m_books;
    // library.json as read, so fields this version doesn't know survive a save.
    QJsonObject m_libraryObject;
    bool m_libraryWritable = true;
    QString m_error;
    QStringList m_storageProblems;
};
