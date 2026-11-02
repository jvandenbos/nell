#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class Library;
class Preferences;
class QNetworkReply;

// Catalogs on the network that serve books as OPDS feeds: a Calibre
// content server, Kavita, a Standard Ebooks feed. Each is a name, a URL,
// and a login, kept in ~/.local/share/nell/catalogs.json. Browsing walks
// the feeds; getting a book downloads it into the downloads folder and adds
// it to the library, where it's then read like any other.
class BookCatalog : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList catalogs READ catalogs NOTIFY catalogsChanged)
    // The feed being looked at: its title, its entries (kind "book" or
    // "nav"), whether there's a page after it, whether it can be searched.
    Q_PROPERTY(QString title READ title NOTIFY feedChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY feedChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY feedChanged)
    Q_PROPERTY(bool searchable READ searchable NOTIFY feedChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY feedChanged)
    Q_PROPERTY(int current READ current NOTIFY feedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // Downloads under way, by acquisition URL, each 0..1.
    Q_PROPERTY(QVariantMap downloads READ downloads NOTIFY downloadsChanged)

public:
    static constexpr int FormatVersion = 1;

    explicit BookCatalog(Library *library, Preferences *preferences, const QString &dataDirectory = {},
                     const QString &cacheDirectory = {}, QObject *parent = nullptr);

    QVariantList catalogs() const { return m_catalogs; }
    QString title() const { return m_title; }
    QVariantList entries() const { return m_entries; }
    bool hasMore() const { return !m_next.isEmpty(); }
    bool searchable() const { return !m_searchTemplate.isEmpty(); }
    bool canGoBack() const { return m_history.size() > 1; }
    int current() const { return m_current; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QVariantMap downloads() const { return m_downloads; }

    // Saves a catalog and opens it. The password is kept with it, in a
    // file only this user can read.
    Q_INVOKABLE bool add(const QString &name, const QString &url, const QString &user, const QString &password);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void open(int index);
    Q_INVOKABLE void browse(const QUrl &url);
    Q_INVOKABLE void back();
    Q_INVOKABLE void more();
    Q_INVOKABLE void search(const QString &terms);
    Q_INVOKABLE void leave();
    // Downloads a book entry's EPUB (or PDF, if that's all it has) and adds
    // it to the library; downloaded() follows with the library id.
    Q_INVOKABLE void get(int entryIndex);
    // A thumbnail from the cache, or empty with coverReady() to follow once
    // it has been fetched.
    Q_INVOKABLE QString cover(const QString &url);
    Q_INVOKABLE void clearError();

    // Reads an OPDS (Atom) feed. Entries with an EPUB or PDF to get are
    // books; the rest lead to other feeds. Relative links resolve against
    // base. Exposed for the tests.
    static QVariantList parse(const QByteArray &atom, const QUrl &base, QString *title, QUrl *next, QUrl *searchUrl,
                              QString *searchTemplate);
    // The OpenSearch description's Atom template, with {searchTerms} in it.
    static QString parseSearchDescription(const QByteArray &xml, const QUrl &base);

signals:
    void catalogsChanged();
    void feedChanged();
    void busyChanged();
    void errorChanged();
    void downloadsChanged();
    void coverReady(const QString &url, const QString &path);
    void downloaded(const QString &id, const QString &title);

private:
    void load();
    bool save();
    void fetch(const QUrl &url, bool append);
    void setBusy(bool busy);
    void setError(const QString &error);
    QNetworkReply *request(const QUrl &url);
    QString downloadPath(const QString &title, const QString &author, const QString &extension) const;

    Library *m_library;
    Preferences *m_preferences;
    QString m_path;
    QString m_cacheDirectory;
    QNetworkAccessManager m_network;
    QVariantList m_catalogs;
    int m_current = -1;
    QList<QUrl> m_history;
    QString m_title;
    QVariantList m_entries;
    QUrl m_next;
    QUrl m_searchUrl;
    QString m_searchTemplate;
    bool m_busy = false;
    QString m_error;
    QVariantMap m_downloads;
    QNetworkReply *m_feedReply = nullptr;
    QHash<QString, QNetworkReply *> m_coverReplies;
};
