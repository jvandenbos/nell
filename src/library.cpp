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
#include <QLockFile>
#include <QSet>
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
    if (!file.exists())
        return problems;
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(LockTimeout) || !file.open(QIODevice::ReadOnly)) {
        problems << QString("Could not open %1 to update its covers.").arg(path);
        return problems;
    }
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

QString Library::fileDigest(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

bool Library::inspect(const QString &path, const QString &cacheDirectory, BookInfo *info, QString *error) {
    const QFileInfo file(path);
    if (!file.isFile()) {
        *error = tr("%1 doesn't exist.").arg(path);
        return false;
    }
    info->path = file.absoluteFilePath();
    info->digest = fileDigest(info->path);
    info->id = info->digest.left(16);
    if (info->id.isEmpty()) {
        *error = tr("%1 can't be read.").arg(path);
        return false;
    }
    const QString suffix = file.suffix().toLower();
    if (suffix == QLatin1String("epub")) {
        Epub epub;
        if (!Epub::extract(info->path, cacheDirectory + "/" + info->id, error, info->digest) ||
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
    const QFileInfo file(info.path);
    const QVariantMap stamp{{"sha256", info.digest},
                            {"size", file.size()},
                            {"modified", file.lastModified().toUTC().toString(Qt::ISODateWithMs)}};
    const int index = indexOf(info.id);
    if (index >= 0) {
        const auto book = m_books[index].toMap();
        if (book["path"] == info.path && book["sha256"] == info.digest && book["size"].toLongLong() == file.size() &&
            book["modified"] == stamp["modified"])
            return info.id;
    }
    auto entry = [&] {
        QVariantMap book{{"id", info.id},          {"path", info.path},   {"format", info.format},
                         {"title", info.title},    {"author", info.author}, {"language", info.language},
                         {"cover", makeCover(info)}, {"progress", 0.0},   {"added", now()},
                         {"opened", QString()},   {"finished", false}};
        book.insert(stamp);
        return book;
    };
    // The same book from somewhere new follows the file. Asked of the
    // library on disk, which another Nell may have added it to meanwhile.
    const bool saved = update([&](QVariantList &books) {
        for (auto &value : books) {
            auto book = value.toMap();
            if (book["id"] != info.id)
                continue;
            book["path"] = info.path;
            book.insert(stamp);
            value = book;
            return;
        }
        books.prepend(entry());
    });
    return saved || index >= 0 ? info.id : QString();
}

int Library::addUrls(const QVariantList &urls) {
    int added = 0;
    for (const auto &url : urls)
        if (!add(url.toUrl().isValid() ? url.toUrl().toString() : url.toString()).isEmpty())
            ++added;
    return added;
}

void Library::remove(const QString &id) {
    if (indexOf(id) < 0)
        return;
    // The book's notes stay in books/<id>.json, so adding it again later
    // brings them back.
    update([&](QVariantList &books) {
        for (qsizetype i = books.size() - 1; i >= 0; --i)
            if (books[i].toMap()["id"] == id)
                books.removeAt(i);
    });
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

bool Library::relocate(const QString &id, const QString &pathOrUrl) {
    setError({});
    const QString path = localPath(pathOrUrl);
    if (indexOf(id) < 0 || fileId(path) != id) {
        setError(tr("%1 isn't the same book.").arg(QFileInfo(path).fileName()));
        return false;
    }
    return add(path) == id;
}

void Library::touch(const QString &id, double progress) {
    if (indexOf(id) < 0)
        return;
    const QString opened = now();
    update([&](QVariantList &books) {
        for (auto &value : books) {
            auto book = value.toMap();
            if (book["id"] != id)
                continue;
            // The newest reading wins; ISO 8601 in UTC sorts as text.
            if (book["opened"].toString() <= opened) {
                book["progress"] = progress;
                book["opened"] = opened;
            }
            if (progress >= 0.995)
                book["finished"] = true;
            value = book;
        }
    });
}

QString Library::checkFile(const QString &id, bool record) {
    const QVariantMap book = this->book(id);
    const QFileInfo file(book["path"].toString());
    const QString modified = file.lastModified().toUTC().toString(Qt::ISODateWithMs);
    if (!book["sha256"].toString().isEmpty() && book["size"].toLongLong() == file.size() && book["modified"] == modified)
        return id;
    const QString digest = fileDigest(file.absoluteFilePath());
    if (digest.isEmpty())
        return {};
    // Books added before 0.4 have only the id, which is the digest's start.
    const QString known = book["sha256"].toString();
    if (known.isEmpty() ? digest.left(16) != id : digest != known)
        return digest.left(16);
    if (record)
        update([&](QVariantList &books) {
            for (auto &value : books) {
                auto entry = value.toMap();
                if (entry["id"] != id)
                    continue;
                entry["sha256"] = digest;
                entry["size"] = file.size();
                entry["modified"] = modified;
                value = entry;
            }
        });
    return id;
}

void Library::markRevision(const QString &oldId, const QString &newId) {
    update([&](QVariantList &books) {
        for (auto &value : books) {
            auto entry = value.toMap();
            if (entry["id"] == oldId)
                entry["replacedBy"] = newId;
            else if (entry["id"] == newId)
                entry["revisionOf"] = oldId;
            else
                continue;
            value = entry;
        }
    });
}

QString Library::findId(const QString &pathOrId) const {
    for (const auto &value : m_books) {
        const auto book = value.toMap();
        if (book["id"] == pathOrId || book["path"] == QFileInfo(pathOrId).absoluteFilePath())
            return book["id"].toString();
    }
    return {};
}

Store Library::readStore(const QString &path) {
    Store store;
    QFile file(path);
    if (!file.exists())
        return store;
    if (!file.open(QIODevice::ReadOnly)) {
        store.state = StoreState::Corrupt;
        store.problem = tr("%1 can't be read: %2").arg(path, file.errorString());
        return store;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        store.state = StoreState::Corrupt;
        store.problem = tr("%1 is damaged (%2), so Nell is leaving it as it is.")
                            .arg(path, error.error != QJsonParseError::NoError ? error.errorString() : tr("not an object"));
        return store;
    }
    store.object = document.object();
    if (store.object["version"].toInt(1) > FormatVersion) {
        store.state = StoreState::Newer;
        store.problem = tr("%1 was written by a newer Nell, so this one only reads it.").arg(path);
        return store;
    }
    store.state = StoreState::Valid;
    return store;
}

Store Library::annotations(const QString &id) const { return readStore(m_dataDirectory + "/books/" + id + ".json"); }

namespace {
// One list of marks merged three ways: base is what this Nell read, ours what
// it has now, theirs what's on disk. A mark only one side changed takes that
// side's change, additions from both are kept, and where both changed the
// same mark ours wins.
QJsonArray mergeMarks(const QJsonArray &base, const QJsonArray &ours, const QJsonArray &theirs,
                      const std::function<QString(const QJsonObject &)> &key) {
    QHash<QString, QJsonObject> baseByKey, oursByKey, theirsByKey;
    for (const auto &value : base)
        baseByKey.insert(key(value.toObject()), value.toObject());
    for (const auto &value : ours)
        oursByKey.insert(key(value.toObject()), value.toObject());
    for (const auto &value : theirs)
        theirsByKey.insert(key(value.toObject()), value.toObject());
    QJsonArray merged;
    for (const auto &value : ours) {
        const QJsonObject mark = value.toObject();
        const QString k = key(mark);
        const bool inBase = baseByKey.contains(k), inTheirs = theirsByKey.contains(k);
        if (inBase && !inTheirs && mark == baseByKey[k])
            continue;  // They removed it and we didn't touch it.
        if (inBase && inTheirs && mark == baseByKey[k])
            merged << theirsByKey[k];  // Only they changed it, if anyone did.
        else
            merged << mark;
    }
    for (const auto &value : theirs) {
        const QString k = key(value.toObject());
        if (!oursByKey.contains(k) && !baseByKey.contains(k))
            merged << value;  // They added it.
    }
    return merged;
}
}

QJsonObject Library::mergeAnnotations(const QJsonObject &base, const QJsonObject &ours, const QJsonObject &theirs) {
    QJsonObject merged = theirs;
    // Settings and anything this Nell doesn't know: ours where we changed
    // it, theirs otherwise. The place is always ours, the reader in front of us.
    QSet<QString> keys;
    for (const auto &object : {base, ours, theirs})
        for (const auto &k : object.keys())
            keys << k;
    for (const auto &k : keys) {
        if (k == QLatin1String("bookmarks") || k == QLatin1String("highlights") || k == QLatin1String("vocabulary"))
            continue;
        if ((k == QLatin1String("position") && ours.contains(k)) || ours.value(k) != base.value(k)) {
            if (ours.contains(k))
                merged[k] = ours[k];
            else
                merged.remove(k);
        }
    }
    auto at = [](const QJsonObject &mark) {
        return QString("%1:%2").arg(mark["chapter"].toInt()).arg(mark["offset"].toInt());
    };
    merged["bookmarks"] = mergeMarks(base["bookmarks"].toArray(), ours["bookmarks"].toArray(),
                                     theirs["bookmarks"].toArray(), at);
    merged["highlights"] = mergeMarks(base["highlights"].toArray(), ours["highlights"].toArray(),
                                      theirs["highlights"].toArray(),
                                      [](const QJsonObject &mark) { return mark["id"].toString(); });
    QJsonArray words = mergeMarks(base["vocabulary"].toArray(), ours["vocabulary"].toArray(),
                                  theirs["vocabulary"].toArray(),
                                  [](const QJsonObject &mark) { return mark["word"].toString().toLower(); });
    // Words are kept newest first.
    QVariantList list = words.toVariantList();
    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["created"].toString() > b.toMap()["created"].toString();
    });
    merged["vocabulary"] = QJsonArray::fromVariantList(list);
    return merged;
}

bool Library::saveAnnotations(const QString &id, const QJsonObject &annotations, const QJsonObject &base,
                              QJsonObject *saved) {
    const QString path = m_dataDirectory + "/books/" + id + ".json";
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(LockTimeout)) {
        setError(tr("Your notes are busy in another Nell; they'll be saved on the next try."));
        return false;
    }
    const Store existing = readStore(path);
    if (!existing.writable()) {
        reportStorageProblem(existing.problem);
        setError(existing.problem);
        return false;
    }
    // Another Nell may have saved this book's notes since we read them.
    QJsonObject merged = existing.state == StoreState::Valid && existing.object != base
                             ? mergeAnnotations(base, annotations, existing.object)
                             : annotations;
    merged["version"] = FormatVersion;
    if (!writeJson(path, merged)) {
        setError(tr("Could not save your place and notes in %1.").arg(m_dataDirectory));
        return false;
    }
    if (saved)
        *saved = merged;
    return true;
}

QString Library::importNotes(const QJsonObject &notes) {
    setError({});
    static const QRegularExpression hex16("^[0-9a-f]{16}$");
    const QString id = notes["id"].toString();
    if (!hex16.match(id).hasMatch() || !notes["highlights"].isArray()) {
        setError(tr("This isn't a Nell notes export."));
        return {};
    }
    // Exports from before the field had it at 1.
    if (notes["version"].toInt(1) > NotesFormatVersion) {
        setError(tr("These notes were exported by a newer Nell."));
        return {};
    }
    // The export adds places worked out for the reader; they aren't stored.
    QJsonObject annotations;
    for (const char *key : {"bookmarks", "highlights", "vocabulary"}) {
        QJsonArray marks = notes[key].toArray();
        for (qsizetype i = 0; i < marks.size(); ++i) {
            QJsonObject mark = marks[i].toObject();
            for (const char *field : {"location", "page", "section", "index", "missing"})
                mark.remove(QLatin1String(field));
            marks[i] = mark;
        }
        annotations[key] = marks;
    }
    for (const char *key : {"position", "fitText"})
        if (notes.contains(key))
            annotations[key] = notes[key];
    // Merged as if from nothing, so what's there already stays alongside.
    return saveAnnotations(id, annotations, QJsonObject()) ? id : QString();
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
    const Store store = readStore(m_dataDirectory + "/library.json");
    m_libraryObject = store.object;
    m_books = store.object["books"].toArray().toVariantList();
    m_libraryWritable = store.writable();
    if (!m_libraryWritable)
        reportStorageProblem(store.problem);
}

// Every change is made to the library as it is on disk at that moment, under
// a lock, so a book another Nell (or `nell add`) saved since this one loaded
// is kept rather than written over. QSaveFile still makes the write itself
// all or nothing.
bool Library::update(const std::function<void(QVariantList &books)> &change) {
    const QString path = m_dataDirectory + "/library.json";
    QDir().mkpath(m_dataDirectory);
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(LockTimeout)) {
        setError(tr("The library is busy in another Nell; try again."));
        return false;
    }
    const Store store = readStore(path);
    if (!store.writable()) {
        m_libraryWritable = false;
        reportStorageProblem(store.problem);
        setError(tr("The library can't be saved: %1").arg(store.problem));
        return false;
    }
    QVariantList books = store.object["books"].toArray().toVariantList();
    change(books);
    QJsonObject object = store.object;
    object["version"] = FormatVersion;
    object["books"] = QJsonArray::fromVariantList(books);
    if (!writeJson(path, object)) {
        setError(tr("Could not save the library in %1.").arg(m_dataDirectory));
        return false;
    }
    m_libraryWritable = true;
    m_libraryObject = object;
    m_books = books;
    emit changed();
    return true;
}

void Library::reportStorageProblem(const QString &problem) {
    if (problem.isEmpty() || m_storageProblems.contains(problem))
        return;
    qWarning("nell: %s", qPrintable(problem));
    m_storageProblems << problem;
    emit storageProblemsChanged();
    emit storageWarning(problem);
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
