#include "epub.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Epub", text); }

// Hrefs in a package are URLs relative to the file that names them.
QString resolve(const QString &baseFile, const QString &href) {
    const QString decoded = QUrl::fromPercentEncoding(href.toUtf8());
    const int hash = decoded.indexOf('#');
    const QString path = hash < 0 ? decoded : decoded.left(hash);
    const QString fragment = hash < 0 ? QString() : decoded.mid(hash);
    const QString file = path.isEmpty() ? baseFile
                                        : QDir::cleanPath(QFileInfo(baseFile).absolutePath() + "/" + path);
    return file + fragment;
}

QString simplified(const QString &text) { return text.simplified(); }
}

namespace {
// Beside the unpacked book: the digest of the file it came from.
QString stampOf(const QString &directory) { return directory + "/.nell-sha256"; }

QString readStamp(const QString &directory) {
    QFile file(stampOf(directory));
    return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll()).trimmed() : QString();
}
}

bool Epub::extract(const QString &file, const QString &directory, QString *error, const QString &digest) {
    if (QFileInfo::exists(directory + "/META-INF/container.xml") && (digest.isEmpty() || readStamp(directory) == digest))
        return true;
    const QString bsdtar = QStandardPaths::findExecutable("bsdtar");
    if (bsdtar.isEmpty()) {
        *error = tr("bsdtar is not installed, so EPUB books can't be opened. Install libarchive.");
        return false;
    }
    const QString partial = directory + ".partial";
    QDir(partial).removeRecursively();
    if (!QDir().mkpath(partial)) {
        *error = tr("Could not create %1").arg(partial);
        return false;
    }
    QProcess process;
    process.start(bsdtar, {"-xf", file, "-C", partial});
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        QDir(partial).removeRecursively();
        *error = tr("This EPUB could not be unpacked: %1").arg(detail.isEmpty() ? process.errorString() : detail);
        return false;
    }
    if (!QFileInfo::exists(partial + "/META-INF/container.xml")) {
        QDir(partial).removeRecursively();
        *error = tr("This file is not an EPUB book.");
        return false;
    }
    if (!digest.isEmpty()) {
        QFile stamp(stampOf(partial));
        if (stamp.open(QIODevice::WriteOnly))
            stamp.write(digest.toLatin1());
    }
    QDir(directory).removeRecursively();
    if (!QDir().rename(partial, directory)) {
        *error = tr("Could not create %1").arg(directory);
        return false;
    }
    return true;
}

bool Epub::load(const QString &directory, QString *error) {
    QFile container(directory + "/META-INF/container.xml");
    if (!container.open(QIODevice::ReadOnly)) {
        *error = tr("This file is not an EPUB book.");
        return false;
    }
    QString opf;
    QXmlStreamReader xml(&container);
    while (!xml.atEnd() && opf.isEmpty())
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("rootfile"))
            opf = xml.attributes().value("full-path").toString();
    if (opf.isEmpty()) {
        *error = tr("This EPUB has no package file.");
        return false;
    }

    QString nav, ncx;
    if (!readPackage(QDir::cleanPath(directory + "/" + QUrl::fromPercentEncoding(opf.toUtf8())), &nav, &ncx, error))
        return false;
    // EPUB 3 books carry a navigation document; EPUB 2 books only an NCX.
    // Some carry both and only one of them is any good, so fall back.
    if (!nav.isEmpty())
        readNav(nav);
    if (toc.isEmpty() && !ncx.isEmpty())
        readNcx(ncx);
    if (spine.isEmpty()) {
        *error = tr("This EPUB has no pages to read.");
        return false;
    }
    return true;
}

bool Epub::readPackage(const QString &opfPath, QString *navPath, QString *ncxPath, QString *error) {
    QFile file(opfPath);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = tr("This EPUB's package file is missing.");
        return false;
    }
    struct Item { QString path, type, properties; };
    QHash<QString, Item> manifest;
    QStringList spineIds;
    QString coverId, ncxId;

    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const auto name = xml.name();
        const auto attributes = xml.attributes();
        if (name == QLatin1String("title") && title.isEmpty())
            title = simplified(xml.readElementText(QXmlStreamReader::IncludeChildElements));
        else if (name == QLatin1String("creator") && author.isEmpty())
            author = simplified(xml.readElementText(QXmlStreamReader::IncludeChildElements));
        else if (name == QLatin1String("language") && language.isEmpty())
            language = simplified(xml.readElementText());
        else if (name == QLatin1String("meta") && attributes.value("name") == QLatin1String("cover"))
            coverId = attributes.value("content").toString();
        else if (name == QLatin1String("item"))
            manifest.insert(attributes.value("id").toString(),
                            {resolve(opfPath, attributes.value("href").toString()),
                             attributes.value("media-type").toString(), attributes.value("properties").toString()});
        else if (name == QLatin1String("spine"))
            ncxId = attributes.value("toc").toString();
        else if (name == QLatin1String("itemref") && attributes.value("linear") != QLatin1String("no"))
            spineIds << attributes.value("idref").toString();
    }
    if (xml.hasError()) {
        *error = tr("This EPUB's package file is damaged: %1").arg(xml.errorString());
        return false;
    }

    for (const auto &id : spineIds)
        if (manifest.contains(id) && QFileInfo::exists(manifest[id].path))
            spine << manifest[id].path;
    for (const auto &item : manifest) {
        const QStringList properties = item.properties.split(' ', Qt::SkipEmptyParts);
        if (properties.contains("nav"))
            *navPath = item.path;
        if (properties.contains("cover-image"))
            coverPath = item.path;
        if (item.type == QLatin1String("application/x-dtbncx+xml") && ncxPath->isEmpty())
            *ncxPath = item.path;
    }
    if (manifest.contains(ncxId))
        *ncxPath = manifest[ncxId].path;
    if (coverPath.isEmpty() && manifest.contains(coverId) && manifest[coverId].type.startsWith("image/"))
        coverPath = manifest[coverId].path;
    if (!coverPath.isEmpty() && !QFileInfo::exists(coverPath))
        coverPath.clear();
    if (title.isEmpty())
        title = QFileInfo(opfPath).completeBaseName();
    return true;
}

void Epub::readNav(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QXmlStreamReader xml(&file);
    int navDepth = 0, listDepth = 0;
    QList<TocEntry> entries;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        const auto name = xml.name();
        if (token == QXmlStreamReader::StartElement) {
            if (name == QLatin1String("nav")) {
                // The navigation document can also hold landmarks and page
                // lists; only the one typed "toc" is the table of contents.
                const bool isToc = xml.attributes().value("http://www.idpf.org/2007/ops", "type") == QLatin1String("toc") ||
                                   xml.attributes().value("epub:type") == QLatin1String("toc");
                if (navDepth || isToc)
                    ++navDepth;
            } else if (navDepth && name == QLatin1String("ol")) {
                ++listDepth;
            } else if (navDepth && name == QLatin1String("a")) {
                const QString href = xml.attributes().value("href").toString();
                const QString text = simplified(xml.readElementText(QXmlStreamReader::IncludeChildElements));
                if (!href.isEmpty() && !text.isEmpty())
                    entries << TocEntry{text, resolve(path, href), qMax(0, listDepth - 1)};
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (navDepth && name == QLatin1String("ol"))
                --listDepth;
            else if (navDepth && name == QLatin1String("nav"))
                --navDepth;
        }
    }
    // A navigation document that isn't well-formed XML is common enough;
    // the NCX fallback is better than half a table of contents.
    if (!xml.hasError())
        toc = entries;
}

void Epub::readNcx(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QXmlStreamReader xml(&file);
    int depth = 0;
    QString label;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        const auto name = xml.name();
        if (token == QXmlStreamReader::StartElement) {
            if (name == QLatin1String("navPoint"))
                ++depth;
            else if (name == QLatin1String("text") && depth)
                label = simplified(xml.readElementText());
            else if (name == QLatin1String("content") && depth) {
                const QString src = xml.attributes().value("src").toString();
                if (!src.isEmpty() && !label.isEmpty())
                    toc << TocEntry{label, resolve(path, src), depth - 1};
                label.clear();
            }
        } else if (token == QXmlStreamReader::EndElement && name == QLatin1String("navPoint")) {
            --depth;
        }
    }
}
