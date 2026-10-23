#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// One entry of a book's table of contents. The target is an absolute file
// path inside the extracted book, with the fragment after a '#'.
struct TocEntry {
    QString title;
    QString target;
    int level = 0;
};

// The package of an EPUB, unpacked into a directory: its metadata, reading
// order, and table of contents. Rendering is the Reader's job.
class Epub {
public:
    // Unpacks the book into directory with bsdtar, which already refuses
    // absolute paths and "..", so a hostile archive can't write outside it.
    // The archive lands in a sibling first and is renamed into place, so a
    // half-unpacked book is never mistaken for a whole one.
    static bool extract(const QString &file, const QString &directory, QString *error);

    bool load(const QString &directory, QString *error);

    QString title;
    QString author;
    QString language;
    QString coverPath;
    QStringList spine;
    QList<TocEntry> toc;

private:
    bool readPackage(const QString &opfPath, QString *navPath, QString *ncxPath, QString *error);
    void readNav(const QString &path);
    void readNcx(const QString &path);
};
