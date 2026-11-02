#include "catalog.h"

#include "library.h"
#include "preferences.h"

#include <QAuthenticator>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextDocumentFragment>
#include <QXmlStreamReader>

namespace {
const char *const Acquisition = "http://opds-spec.org/acquisition";

QString plainText(const QString &text, bool html) {
    const QString plain = html ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
    return plain.simplified();
}

bool isEpub(const QString &type) { return type.startsWith("application/epub+zip"); }
bool isPdf(const QString &type) { return type.startsWith("application/pdf"); }
bool isFeed(const QString &type) { return type.startsWith("application/atom+xml"); }
}  // namespace

BookCatalog::BookCatalog(Library *library, Preferences *preferences, const QString &dataDirectory, const QString &cacheDirectory,
                 QObject *parent)
    : QObject(parent), m_library(library), m_preferences(preferences),
      m_path((dataDirectory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/nell"
                                      : dataDirectory) +
             "/catalogs.json"),
      m_cacheDirectory((cacheDirectory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/nell"
                                                 : cacheDirectory) +
                       "/catalog") {
    m_network.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    // The current catalog's login, offered once per challenge; a second
    // challenge for the same reply means it was refused.
    connect(&m_network, &QNetworkAccessManager::authenticationRequired, this,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
                if (m_current < 0 || reply->property("authenticated").toBool())
                    return;
                const QVariantMap catalog = m_catalogs[m_current].toMap();
                authenticator->setUser(catalog["user"].toString());
                authenticator->setPassword(catalog["password"].toString());
                reply->setProperty("authenticated", true);
            });
    load();
}

void BookCatalog::load() {
    const Store store = Library::readStore(m_path);
    if (!store.writable() && m_library)
        m_library->reportStorageProblem(store.problem);
    m_catalogs = store.object["catalogs"].toArray().toVariantList();
}

bool BookCatalog::save() {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(tr("Could not save the catalogs in %1.").arg(QFileInfo(m_path).absolutePath()));
        return false;
    }
    // Logins are in here, so nobody else on the machine gets to read it.
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.write(QJsonDocument(QJsonObject{{"version", FormatVersion}, {"catalogs", QJsonArray::fromVariantList(m_catalogs)}})
                   .toJson());
    if (!file.commit()) {
        setError(tr("Could not save the catalogs in %1.").arg(QFileInfo(m_path).absolutePath()));
        return false;
    }
    QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool BookCatalog::add(const QString &name, const QString &url, const QString &user, const QString &password) {
    QUrl parsed = QUrl::fromUserInput(url.trimmed());
    if (!parsed.isValid() || parsed.host().isEmpty() && parsed.scheme() != "file") {
        setError(tr("That isn't a web address."));
        return false;
    }
    const QString title = name.trimmed().isEmpty() ? parsed.host() : name.trimmed();
    m_catalogs << QVariantMap{{"name", title}, {"url", parsed.toString()}, {"user", user.trimmed()}, {"password", password}};
    if (!save()) {
        m_catalogs.removeLast();
        return false;
    }
    emit catalogsChanged();
    open(m_catalogs.size() - 1);
    return true;
}

void BookCatalog::remove(int index) {
    if (index < 0 || index >= m_catalogs.size())
        return;
    m_catalogs.removeAt(index);
    save();
    if (m_current == index)
        leave();
    else if (m_current > index)
        --m_current;
    emit catalogsChanged();
}

void BookCatalog::open(int index) {
    if (index < 0 || index >= m_catalogs.size())
        return;
    m_current = index;
    m_history.clear();
    browse(QUrl(m_catalogs[index].toMap()["url"].toString()));
}

void BookCatalog::leave() {
    if (m_feedReply)
        m_feedReply->abort();
    m_current = -1;
    m_history.clear();
    m_title.clear();
    m_entries.clear();
    m_next.clear();
    m_searchUrl.clear();
    m_searchTemplate.clear();
    emit feedChanged();
}

void BookCatalog::browse(const QUrl &url) {
    m_history << url;
    fetch(url, false);
}

void BookCatalog::back() {
    if (m_history.size() < 2)
        return;
    m_history.removeLast();
    fetch(m_history.last(), false);
}

void BookCatalog::more() {
    if (!m_next.isEmpty())
        fetch(m_next, true);
}

void BookCatalog::search(const QString &terms) {
    if (m_searchTemplate.isEmpty() || terms.trimmed().isEmpty())
        return;
    QString url = m_searchTemplate;
    url.replace("{searchTerms}", QString::fromUtf8(QUrl::toPercentEncoding(terms.trimmed())));
    // Optional OpenSearch parameters nobody filled in.
    url.remove(QRegularExpression("\\{[^}]*\\?\\}"));
    browse(QUrl(url));
}

QNetworkReply *BookCatalog::request(const QUrl &url) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString("Nell/%1").arg(QString::fromLatin1(NELL_VERSION)));
    request.setTransferTimeout(30000);
    return m_network.get(request);
}

void BookCatalog::fetch(const QUrl &url, bool append) {
    if (m_feedReply)
        m_feedReply->abort();
    setError({});
    setBusy(true);
    QNetworkReply *reply = request(url);
    m_feedReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, append, url] {
        reply->deleteLater();
        if (m_feedReply != reply)
            return;  // Overtaken by a later request.
        m_feedReply = nullptr;
        setBusy(false);
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            setError(status == 401 || status == 403 ? tr("The catalog refused the login.")
                                                    : tr("The catalog couldn't be read: %1").arg(reply->errorString()));
            if (!append && !m_history.isEmpty() && m_history.last() == url && m_history.size() > 1)
                m_history.removeLast();
            return;
        }
        QString title;
        QUrl next, searchUrl;
        QString searchTemplate;
        const QVariantList entries = parse(reply->readAll(), reply->url(), &title, &next, &searchUrl, &searchTemplate);
        if (append) {
            m_entries += entries;
        } else {
            m_entries = entries;
            m_title = title.isEmpty() && m_current >= 0 ? m_catalogs[m_current].toMap()["name"].toString() : title;
            if (!searchTemplate.isEmpty())
                m_searchTemplate = searchTemplate;
        }
        m_next = next;
        emit feedChanged();
        if (m_searchTemplate.isEmpty() && !searchUrl.isEmpty()) {
            QNetworkReply *description = request(searchUrl);
            connect(description, &QNetworkReply::finished, this, [this, description, searchUrl] {
                description->deleteLater();
                if (description->error() != QNetworkReply::NoError)
                    return;
                const QString found = parseSearchDescription(description->readAll(), searchUrl);
                if (!found.isEmpty() && m_current >= 0) {
                    m_searchTemplate = found;
                    emit feedChanged();
                }
            });
        }
    });
}

QVariantList BookCatalog::parse(const QByteArray &atom, const QUrl &base, QString *title, QUrl *next, QUrl *searchUrl,
                            QString *searchTemplate) {
    QVariantList entries;
    QXmlStreamReader xml(atom);
    auto resolve = [&](const QString &href) { return base.resolved(QUrl(href)); };
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("feed"))
            return entries;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("title")) {
                if (title)
                    *title = xml.readElementText().simplified();
            } else if (xml.name() == QLatin1String("link")) {
                const auto attributes = xml.attributes();
                const QString rel = attributes.value("rel").toString();
                const QString type = attributes.value("type").toString();
                const QString href = attributes.value("href").toString();
                if (rel == "next" && next)
                    *next = resolve(href);
                else if (rel == "search" && type.contains("opensearchdescription") && searchUrl)
                    *searchUrl = resolve(href);
                else if (rel == "search" && href.contains("{searchTerms}") && searchTemplate)
                    *searchTemplate = resolve(href).toString(QUrl::FullyEncoded).replace("%7BsearchTerms%7D", "{searchTerms}");
                xml.skipCurrentElement();
            } else if (xml.name() == QLatin1String("entry")) {
                QVariantMap entry{{"kind", "nav"}, {"title", ""}, {"author", ""}, {"summary", ""},
                                  {"thumbnail", ""}, {"epub", ""}, {"pdf", ""}, {"url", ""}};
                QStringList authors;
                QString summary, content;
                while (xml.readNextStartElement()) {
                    const QString name = xml.name().toString();
                    if (name == "title") {
                        entry["title"] = xml.readElementText().simplified();
                    } else if (name == "author") {
                        while (xml.readNextStartElement()) {
                            if (xml.name() == QLatin1String("name"))
                                authors << xml.readElementText().simplified();
                            else
                                xml.skipCurrentElement();
                        }
                    } else if (name == "summary") {
                        const bool html = xml.attributes().value("type").toString().contains("html");
                        summary = plainText(xml.readElementText(), html);
                    } else if (name == "content") {
                        const bool html = xml.attributes().value("type").toString().contains("html");
                        content = plainText(xml.readElementText(QXmlStreamReader::IncludeChildElements), html);
                    } else if (name == "link") {
                        const auto attributes = xml.attributes();
                        const QString rel = attributes.value("rel").toString();
                        const QString type = attributes.value("type").toString();
                        const QUrl href = resolve(attributes.value("href").toString());
                        if (rel.startsWith(Acquisition)) {
                            if (isEpub(type) && entry["epub"].toString().isEmpty())
                                entry["epub"] = href.toString();
                            else if (isPdf(type) && entry["pdf"].toString().isEmpty())
                                entry["pdf"] = href.toString();
                        } else if (rel.contains("thumbnail")) {
                            entry["thumbnail"] = href.toString();
                        } else if ((rel.contains("/image") || rel.contains("/cover")) && entry["thumbnail"].toString().isEmpty()) {
                            entry["thumbnail"] = href.toString();
                        } else if ((isFeed(type) || rel == "subsection" || rel.startsWith("http://opds-spec.org/sort")) &&
                                   entry["url"].toString().isEmpty()) {
                            entry["url"] = href.toString();
                        }
                        xml.skipCurrentElement();
                    } else {
                        xml.skipCurrentElement();
                    }
                }
                entry["author"] = authors.join(", ");
                entry["summary"] = summary.isEmpty() ? content : summary;
                if (!entry["epub"].toString().isEmpty() || !entry["pdf"].toString().isEmpty())
                    entry["kind"] = "book";
                else if (entry["url"].toString().isEmpty())
                    continue;  // Neither a book nor a way on: nothing to show.
                entries << entry;
            } else {
                xml.skipCurrentElement();
            }
        }
    }
    return entries;
}

QString BookCatalog::parseSearchDescription(const QByteArray &data, const QUrl &base) {
    QXmlStreamReader xml(data);
    QString fallback;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("Url"))
            continue;
        const QString type = xml.attributes().value("type").toString();
        const QString url = base.resolved(QUrl(xml.attributes().value("template").toString()))
                                .toString(QUrl::FullyEncoded)
                                .replace("%7BsearchTerms%7D", "{searchTerms}");
        if (!url.contains("{searchTerms}"))
            continue;
        if (isFeed(type))
            return url;
        if (fallback.isEmpty())
            fallback = url;
    }
    return fallback;
}

QString BookCatalog::cover(const QString &url) {
    if (url.isEmpty())
        return {};
    const QString path = m_cacheDirectory + "/" +
                         QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex()) + ".img";
    if (QFile::exists(path))
        return path;
    if (m_coverReplies.contains(url))
        return {};
    QNetworkReply *reply = request(QUrl(url));
    m_coverReplies[url] = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, path] {
        reply->deleteLater();
        m_coverReplies.remove(url);
        if (reply->error() != QNetworkReply::NoError)
            return;
        QDir().mkpath(m_cacheDirectory);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write(reply->readAll());
        if (file.commit())
            emit coverReady(url, path);
    });
    return {};
}

QString BookCatalog::downloadPath(const QString &title, const QString &author, const QString &extension) const {
    const QString directory = m_preferences ? m_preferences->downloadDirectory()
                                            : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Books";
    QString name = (author.isEmpty() ? title : title + " - " + author).simplified();
    name.replace(QRegularExpression("[\\\\/:*?\"<>|]"), " ");
    name = name.simplified().left(120);
    if (name.isEmpty())
        name = "book";
    QString path = directory + "/" + name + "." + extension;
    for (int i = 2; QFile::exists(path); ++i)
        path = directory + "/" + name + " (" + QString::number(i) + ")." + extension;
    return path;
}

void BookCatalog::get(int entryIndex) {
    if (entryIndex < 0 || entryIndex >= m_entries.size())
        return;
    const QVariantMap entry = m_entries[entryIndex].toMap();
    const bool epub = !entry["epub"].toString().isEmpty();
    const QString url = epub ? entry["epub"].toString() : entry["pdf"].toString();
    if (url.isEmpty() || m_downloads.contains(url))
        return;
    const QString path = downloadPath(entry["title"].toString(), entry["author"].toString(), epub ? "epub" : "pdf");
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        setError(tr("Could not make the downloads folder %1.").arg(QFileInfo(path).absolutePath()));
        return;
    }
    setError({});
    m_downloads[url] = 0.0;
    emit downloadsChanged();
    QNetworkReply *reply = request(QUrl(url));
    connect(reply, &QNetworkReply::downloadProgress, this, [this, url](qint64 received, qint64 total) {
        m_downloads[url] = total > 0 ? double(received) / total : 0.0;
        emit downloadsChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, path, entry] {
        reply->deleteLater();
        m_downloads.remove(url);
        emit downloadsChanged();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            setError(status == 401 || status == 403 ? tr("The catalog refused the login.")
                                                    : tr("“%1” couldn't be downloaded: %2").arg(entry["title"].toString(), reply->errorString()));
            return;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0 || !file.commit()) {
            setError(tr("Could not write %1.").arg(path));
            return;
        }
        const QString id = m_library ? m_library->add(path) : QString();
        if (id.isEmpty() && m_library) {
            setError(m_library->error());
            m_library->clearError();
            return;
        }
        emit downloaded(id, entry["title"].toString());
    });
}

void BookCatalog::setBusy(bool busy) {
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void BookCatalog::setError(const QString &error) {
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

void BookCatalog::clearError() { setError({}); }
