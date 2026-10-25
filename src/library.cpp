#include "library.h"

#include "epub.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <poppler-qt6.h>

namespace {
QString defaultDirectory(QStandardPaths::StandardLocation location) {
    return QStandardPaths::writableLocation(location) + "/nell";
}

QString localPath(const QString &pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

// "the_great-gatsby" reads as "the great gatsby" when the file names the book.
QString titleFromFile(const QFileInfo &file) {
    QString title = file.completeBaseName();
    title.replace('_', ' ').replace(QRegularExpression(R"((?<=\w)-(?=\w))"), " ");
    return title.simplified();
}
}

namespace {
bool isEmptyDirectory(const QString &path) {
    return QDir(path).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
}

// Moves root/quire to root/nell unless Nell already has something of its own
// there. An empty nell directory (left by a run that stopped half way, or made
// by hand) doesn't count as Nell's own. Returns false only when it tried and
// failed; nothing to move is success.
bool adoptDirectory(const QString &root, QStringList *problems) {
    QDir dir(root);
    if (!dir.exists("quire"))
        return true;
    if (dir.exists("nell")) {
        if (!isEmptyDirectory(dir.filePath("nell")))
            return true;
        dir.rmdir("nell");
    }
    if (dir.rename("quire", "nell"))
        return true;
    *problems << QString("Could not move %1 to %2.").arg(dir.filePath("quire"), dir.filePath("nell"));
    return false;
}
}

QStringList Library::adoptQuire(const QString &dataRoot, const QString &configRoot, const QString &cacheRoot) {
    // Each part is done, or redone, on its own, so a run that stopped half
    // way is finished by the next one rather than left as it was.
    QStringList problems;

    // Settings: the directory, then the file inside it, which keeps its old
    // name until renamed.
    adoptDirectory(configRoot, &problems);
    const QString oldSettings = configRoot + "/nell/quire.conf", settings = configRoot + "/nell/nell.conf";
    if (QFileInfo::exists(oldSettings) && !QFileInfo::exists(settings) && !QFile::rename(oldSettings, settings))
        problems << QString("Could not rename %1 to %2.").arg(oldSettings, settings);

    // The cache can always be made again, so failing to move it is no
    // problem. Covers are only made when a book is added, though, so any
    // left behind (Nell had a cache already, or the move failed) come over
    // one by one.
    QStringList cacheProblems;
    adoptDirectory(cacheRoot, &cacheProblems);
    const QDir oldCovers(cacheRoot + "/quire/covers");
    if (oldCovers.exists() && QDir().mkpath(cacheRoot + "/nell/covers"))
        for (const auto &name : oldCovers.entryList(QDir::Files))
            if (!QFileInfo::exists(cacheRoot + "/nell/covers/" + name))
                QFile::copy(oldCovers.filePath(name), cacheRoot + "/nell/covers/" + name);

    adoptDirectory(dataRoot, &problems);

    // The library points its covers into the cache. Rewriting them is its own
    // step, done whenever an old path is left, whichever run moved what.
    const QString path = dataRoot + "/nell/library.json";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return problems;
    const QByteArray bytes = file.readAll();
    file.close();
    const QString from = cacheRoot + "/quire/", to = cacheRoot + "/nell/";
    if (!bytes.contains(from.toUtf8()))
        return problems;
    QJsonParseError error;
    QJsonObject object = QJsonDocument::fromJson(bytes, &error).object();
    if (error.error != QJsonParseError::NoError || object["version"].toInt(1) > FormatVersion) {
        problems << QString("Left the covers in %1 as they were: it can't be read safely.").arg(path);
        return problems;
    }
    QJsonArray books = object["books"].toArray();
    for (qsizetype i = 0; i < books.size(); ++i) {
        QJsonObject book = books[i].toObject();
        const QString cover = book["cover"].toString();
        if (cover.startsWith(from))
            book["cover"] = to + cover.mid(from.size());
        books[i] = book;
    }
    object["books"] = books;
    if (!writeJson(path, object))
        problems << QString("Could not update the covers in %1.").arg(path);
    return problems;
}

bool Library::quireWaiting(const QString &dataRoot) {
    QDir dir(dataRoot);
    return dir.exists("quire") && (!dir.exists("nell") || isEmptyDirectory(dir.filePath("nell")));
}

Library::Library(const QString &dataDirectory, const QString &cacheDirectory, QObject *parent)
    : QObject(parent),
      m_dataDirectory(dataDirectory.isEmpty() ? defaultDirectory(QStandardPaths::GenericDataLocation) : dataDirectory),
      m_cacheDirectory(cacheDirectory.isEmpty() ? defaultDirectory(QStandardPaths::GenericCacheLocation) : cacheDirectory) {
    load();
}

QString Library::fileId(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex().left(16));
}

bool Library::inspect(const QString &path, const QString &cacheDirectory, BookInfo *info, QString *error) {
    const QFileInfo file(path);
    if (!file.isFile()) {
        *error = tr("%1 doesn't exist.").arg(path);
        return false;
    }
    info->path = file.absoluteFilePath();
    info->id = fileId(info->path);
    if (info->id.isEmpty()) {
        *error = tr("%1 can't be read.").arg(path);
        return false;
    }
    const QString suffix = file.suffix().toLower();
    if (suffix == QLatin1String("epub")) {
        Epub epub;
        if (!Epub::extract(info->path, cacheDirectory + "/" + info->id, error) ||
            !epub.load(cacheDirectory + "/" + info->id, error))
            return false;
        info->format = "epub";
        info->title = epub.title;
        info->author = epub.author;
        info->language = epub.language;
        info->coverSource = epub.coverPath;
        return true;
    }
    if (suffix == QLatin1String("pdf")) {
        auto document = Poppler::Document::load(info->path);
        if (!document || document->isLocked()) {
            *error = document ? tr("This PDF is password protected.") : tr("This PDF is damaged or not a PDF.");
            return false;
        }
        info->format = "pdf";
        info->title = document->info("Title").simplified();
        info->author = document->info("Author").simplified();
        info->pages = document->numPages();
        if (info->title.isEmpty())
            info->title = titleFromFile(file);
        return true;
    }
    *error = suffix.isEmpty() ? tr("Nell reads EPUB and PDF books, and this is neither.")
                              : tr("Nell reads EPUB and PDF books, not .%1 files.").arg(suffix);
    return false;
}

QString Library::makeCover(const BookInfo &info) {
    QImage image;
    if (info.format == QLatin1String("epub") && !info.coverSource.isEmpty()) {
        image.load(info.coverSource);
    } else if (info.format == QLatin1String("pdf")) {
        auto document = Poppler::Document::load(info.path);
        if (document && !document->isLocked() && document->numPages() > 0) {
            document->setRenderHint(Poppler::Document::Antialiasing);
            document->setRenderHint(Poppler::Document::TextAntialiasing);
            const auto page = document->page(0);
            const double dpi = 600.0 / qMax(1.0, page->pageSizeF().height()) * 72.0;
            image = page->renderToImage(dpi, dpi);
        }
    }
    if (image.isNull())
        return {};
    const QString path = m_cacheDirectory + "/covers/" + info.id + ".jpg";
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (image.height() > 600)
        image = image.scaledToHeight(600, Qt::SmoothTransformation);
    return image.convertToFormat(QImage::Format_RGB32).save(path, "JPG", 90) ? path : QString();
}

QString Library::add(const QString &pathOrUrl) {
    setError({});
    const QString path = localPath(pathOrUrl);
    BookInfo info;
    QString error;
    if (!inspect(path, m_cacheDirectory, &info, &error)) {
        setError(error);
        return {};
    }
    const int existing = indexOf(info.id);
    if (existing >= 0) {
        // The same book from somewhere new: follow the file.
        auto book = m_books[existing].toMap();
        if (book["path"].toString() != info.path) {
            book["path"] = info.path;
            m_books[existing] = book;
            save();
            emit changed();
        }
        return info.id;
    }
    QVariantMap book{{"id", info.id},          {"path", info.path},   {"format", info.format},
                     {"title", info.title},    {"author", info.author}, {"language", info.language},
                     {"cover", makeCover(info)}, {"progress", 0.0},   {"added", now()},
                     {"opened", QString()},   {"finished", false}};
    m_books.prepend(book);
    if (!save()) {
        m_books.removeFirst();
        return {};
    }
    emit changed();
    return info.id;
}

int Library::addUrls(const QVariantList &urls) {
    int added = 0;
    for (const auto &url : urls)
        if (!add(url.toUrl().isValid() ? url.toUrl().toString() : url.toString()).isEmpty())
            ++added;
    return added;
}

void Library::remove(const QString &id) {
    const int index = indexOf(id);
    if (index < 0)
        return;
    // The book's notes stay in books/<id>.json, so adding it again later
    // brings them back.
    m_books.removeAt(index);
    save();
    emit changed();
}

QVariantMap Library::book(const QString &id) const {
    const int index = indexOf(id);
    if (index < 0)
        return {};
    auto book = m_books[index].toMap();
    book["missing"] = !QFileInfo::exists(book["path"].toString());
    return book;
}

QVariantList Library::sorted(const QString &sort, const QString &filter) const {
    QVariantList books;
    for (const auto &value : m_books) {
        auto book = value.toMap();
        if (!filter.isEmpty() && !book["title"].toString().contains(filter, Qt::CaseInsensitive) &&
            !book["author"].toString().contains(filter, Qt::CaseInsensitive))
            continue;
        book["missing"] = !QFileInfo::exists(book["path"].toString());
        books << book;
    }
    auto key = [&sort](const QVariantMap &book) {
        if (sort == QLatin1String("title"))
            return book["title"].toString().toLower();
        if (sort == QLatin1String("author"))
            return book["author"].toString().toLower() + "\n" + book["title"].toString().toLower();
        // Recent: last opened, then newest added, both descending.
        const QString opened = book["opened"].toString();
        return QString(opened.isEmpty() ? book["added"].toString() : opened);
    };
    const bool descending = sort != QLatin1String("title") && sort != QLatin1String("author");
    std::stable_sort(books.begin(), books.end(), [&](const QVariant &a, const QVariant &b) {
        return descending ? key(a.toMap()) > key(b.toMap()) : key(a.toMap()) < key(b.toMap());
    });
    return books;
}

void Library::clearError() { setError({}); }

void Library::touch(const QString &id, double progress) {
    const int index = indexOf(id);
    if (index < 0)
        return;
    auto book = m_books[index].toMap();
    book["progress"] = progress;
    book["opened"] = now();
    if (progress >= 0.995)
        book["finished"] = true;
    m_books[index] = book;
    save();
    emit changed();
}

QString Library::findId(const QString &pathOrId) const {
    for (const auto &value : m_books) {
        const auto book = value.toMap();
        if (book["id"] == pathOrId || book["path"] == QFileInfo(pathOrId).absoluteFilePath())
            return book["id"].toString();
    }
    return {};
}

QJsonObject Library::annotations(const QString &id) const {
    QFile file(m_dataDirectory + "/books/" + id + ".json");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool Library::saveAnnotations(const QString &id, QJsonObject annotations) {
    annotations["version"] = FormatVersion;
    if (writeJson(m_dataDirectory + "/books/" + id + ".json", annotations))
        return true;
    setError(tr("Could not save your place and notes in %1.").arg(m_dataDirectory));
    return false;
}

// QSaveFile writes beside the target and renames it into place, so a crash
// mid-write never leaves a half-written library.
bool Library::writeJson(const QString &path, const QJsonObject &object) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(object).toJson());
    return file.commit();
}

void Library::load() {
    QFile file(m_dataDirectory + "/library.json");
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    m_books = object["books"].toArray().toVariantList();
}

bool Library::save() {
    QJsonObject object{{"version", FormatVersion}, {"books", QJsonArray::fromVariantList(m_books)}};
    if (writeJson(m_dataDirectory + "/library.json", object))
        return true;
    setError(tr("Could not save the library in %1.").arg(m_dataDirectory));
    return false;
}

void Library::setError(const QString &message) {
    if (message == m_error)
        return;
    m_error = message;
    emit errorChanged();
}

int Library::indexOf(const QString &id) const {
    for (int i = 0; i < m_books.size(); ++i)
        if (m_books[i].toMap()["id"] == id)
            return i;
    return -1;
}
