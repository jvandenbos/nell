#pragma once

#include <QJsonObject>
#include <QObject>
#include <QVariantList>

// What the library knows about one book file without opening it for reading.
struct BookInfo {
    QString id;
    QString path;
    QString format;  // "epub" or "pdf"
    QString title;
    QString author;
    QString language;
    QString coverSource;  // A file inside the unpacked EPUB, if it has one.
    int pages = 0;        // PDFs only; an EPUB's pages depend on the window.
};

// The books the reader has added, kept in ~/.local/share/quire/library.json,
// with each book's place, bookmarks and highlights beside it in books/<id>.json.
// Books are referenced where they live and never modified; a book's id is a
// hash of its contents, so moving or re-adding the file finds its notes again.
class Library : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList books READ books NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static constexpr int FormatVersion = 1;

    explicit Library(const QString &dataDirectory = {}, const QString &cacheDirectory = {}, QObject *parent = nullptr);

    QVariantList books() const { return m_books; }
    QString error() const { return m_error; }

    // Adds a file or file:// URL, or finds it if it's already there, and
    // returns its id; an empty id means it failed and error says why.
    Q_INVOKABLE QString add(const QString &pathOrUrl);
    Q_INVOKABLE int addUrls(const QVariantList &urls);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE QVariantMap book(const QString &id) const;
    Q_INVOKABLE QVariantList sorted(const QString &sort, const QString &filter) const;
    Q_INVOKABLE void clearError();

    void touch(const QString &id, double progress);
    QString findId(const QString &pathOrId) const;

    QString cacheRoot() const { return m_cacheDirectory; }
    QString cacheDirectory(const QString &id) const { return m_cacheDirectory + "/" + id; }
    QJsonObject annotations(const QString &id) const;
    bool saveAnnotations(const QString &id, QJsonObject annotations);

    static QString fileId(const QString &path);
    static bool inspect(const QString &path, const QString &cacheDirectory, BookInfo *info, QString *error);
    static bool writeJson(const QString &path, const QJsonObject &object);

signals:
    void changed();
    void errorChanged();

private:
    void load();
    bool save();
    void setError(const QString &message);
    int indexOf(const QString &id) const;
    QString makeCover(const BookInfo &info);

    QString m_dataDirectory;
    QString m_cacheDirectory;
    QVariantList m_books;
    QString m_error;
};
