#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include "dictionary.h"
#include "epub.h"
#include "library.h"
#include "preferences.h"
#include "reader.h"
#include "theme.h"
#include "translator.h"

class NellTests : public QObject {
    Q_OBJECT

private slots:
    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsColors()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\nbackground = \"#000011\"\nforeground = \"#eeeeee\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.background(), QString("#000011"));
        QCOMPARE(theme.foreground(), QString("#eeeeee"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
    }

    void shippedCatalogsAreValid()
    {
        // Every catalog parses, keeps its placeholders, and gives each
        // counted string the forms its language needs.
        const QDir catalogs(QFINDTESTDATA("../src/i18n"));
        QVERIFY(catalogs.exists());
        for (const auto &name : catalogs.entryList({"*.json"})) {
            QFile file(catalogs.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            const auto object = QJsonDocument::fromJson(file.readAll(), &error).object();
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(name + ": " + error.errorString()));
            for (auto it = object.begin(); it != object.end(); ++it) {
                if (it.value().isString())
                    for (const char *placeholder : {"%1", "%2", "%n"})
                        QVERIFY2(it.key().contains(placeholder) == it.value().toString().contains(placeholder),
                                 qPrintable(name + ": " + it.key()));
                if (it.value().isObject())
                    QVERIFY2(it.value().toObject().contains("other"), qPrintable(name + ": " + it.key()));
            }
        }
    }

    void epubReadsPackage()
    {
        QTemporaryDir dir;
        const QString book = makeEpub(dir.path());
        Epub epub;
        QString error;
        QVERIFY2(Epub::extract(book, dir.path() + "/unpacked", &error), qPrintable(error));
        QVERIFY2(epub.load(dir.path() + "/unpacked", &error), qPrintable(error));
        QCOMPARE(epub.title, QString("The Test Book"));
        QCOMPARE(epub.author, QString("Ada Writer"));
        QCOMPARE(epub.language, QString("en"));
        QCOMPARE(epub.spine.size(), 2);
        QCOMPARE(epub.toc.size(), 3);
        QCOMPARE(epub.toc[2].title, QString("A Section"));
        QCOMPARE(epub.toc[2].level, 1);
        QVERIFY(epub.toc[2].target.endsWith("two.xhtml#section"));
        QVERIFY(epub.coverPath.endsWith("cover.png"));
    }

    void epubRejectsOtherFiles()
    {
        QTemporaryDir dir;
        write(dir.path() + "/fake.epub", "not a zip");
        QString error;
        QVERIFY(!Epub::extract(dir.path() + "/fake.epub", dir.path() + "/out", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(dir.path() + "/out"));
    }

    void libraryAddsBooksOnce()
    {
        QTemporaryDir dir;
        const QString book = makeEpub(dir.path());
        Library library(dir.path() + "/data", dir.path() + "/cache");
        const QString id = library.add(book);
        QVERIFY2(!id.isEmpty(), qPrintable(library.error()));
        QCOMPARE(library.add(QUrl::fromLocalFile(book).toString()), id);
        QCOMPARE(library.books().size(), 1);
        QCOMPARE(library.book(id)["title"].toString(), QString("The Test Book"));
        QVERIFY(QFileInfo::exists(library.book(id)["cover"].toString()));

        // A moved book is the same book.
        QVERIFY(QFile::rename(book, dir.path() + "/moved.epub"));
        QVERIFY(library.book(id)["missing"].toBool());
        QCOMPARE(library.add(dir.path() + "/moved.epub"), id);
        QCOMPARE(library.books().size(), 1);
        QVERIFY(!library.book(id)["missing"].toBool());

        // It survives a restart.
        Library again(dir.path() + "/data", dir.path() + "/cache");
        QCOMPARE(again.books().size(), 1);
    }

    void libraryReportsWhatItCantRead()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        write(dir.path() + "/notes.txt", "hello");
        QVERIFY(library.add(dir.path() + "/notes.txt").isEmpty());
        QVERIFY(library.error().contains(".txt"));
        QVERIFY(library.add(dir.path() + "/nothing.epub").isEmpty());
        QVERIFY(library.error().contains("doesn't exist"));
        QVERIFY(library.books().isEmpty());
    }

    void librarySortsAndFilters()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        QVERIFY(!library.add(makePdf(dir.path() + "/zebra_notes.pdf", 1)).isEmpty());
        QVERIFY(!library.add(makeEpub(dir.path())).isEmpty());
        const auto byTitle = library.sorted("title", {});
        QCOMPARE(byTitle[0].toMap()["title"].toString(), QString("The Test Book"));
        QCOMPARE(byTitle[1].toMap()["title"].toString(), QString("zebra notes"));
        QCOMPARE(library.sorted("recent", "ada").size(), 1);
    }

    void readerPaginatesAndKeepsItsPlace()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString id = library.add(makeEpub(dir.path()));
        int pages = 0, chapterTwo = 0;
        {
            Reader reader(&library, &preferences);
            QVERIFY2(reader.openBook(id), qPrintable(reader.error()));
            reader.setGeometry(400, 500, 1, 0);
            pages = reader.pageCount();
            QVERIFY(pages > 4);
            QCOMPARE(reader.page(), 0);
            reader.next();
            QCOMPARE(reader.page(), 1);
            reader.previous();
            QCOMPARE(reader.page(), 0);

            reader.goToToc(1);
            chapterTwo = reader.page();
            QVERIFY(chapterTwo > 0);
            QCOMPARE(reader.chapterTitle(), QString("Chapter Two"));
            reader.goToToc(2);
            QVERIFY(reader.page() > chapterTwo);
            QCOMPARE(reader.chapterTitle(), QString("A Section"));
            reader.goToToc(1);
        }
        // Closing saves the place, and opening returns to it.
        Reader reader(&library, &preferences);
        QVERIFY(reader.openBook(id));
        reader.setGeometry(400, 500, 1, 0);
        QCOMPARE(reader.page(), chapterTwo);
        QVERIFY(library.book(id)["progress"].toDouble() > 0);

        // Bigger text means more pages, and the same place in the text.
        preferences.setFontSize(30);
        QVERIFY(reader.pageCount() > pages);
        QCOMPARE(reader.chapterTitle(), QString("Chapter Two"));

        // Two columns show two pages and turn two at a time.
        reader.setGeometry(820, 500, 2, 20);
        // Resizes are paginated once they settle.
        QTRY_COMPARE(reader.page() % 2, 0);
        QCOMPARE(reader.columns(), 2);
        const int before = reader.page();
        reader.next();
        QCOMPARE(reader.page(), before + 2);
    }

    void readerHighlightsNotesAndUndoes()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString id = library.add(makeEpub(dir.path()));
        {
            Reader reader(&library, &preferences);
            QVERIFY(reader.openBook(id));
            reader.setGeometry(400, 500, 1, 0);
            // Half a word on either end selects whole words.
            reader.select(0, 2, 9);
            QCOMPARE(reader.selectionText(), QString("Chapter One"));
            const QString highlight = reader.highlight("green");
            QVERIFY(!highlight.isEmpty());
            reader.setNote(highlight, "  A note.  ");
            QCOMPARE(reader.highlights().size(), 1);
            QCOMPARE(reader.highlights()[0].toMap()["note"].toString(), QString("A note."));
            QCOMPARE(reader.highlights()[0].toMap()["section"].toString(), QString("Chapter One"));
            reader.undo();
            QCOMPARE(reader.highlights()[0].toMap()["note"].toString(), QString());
            reader.redo();
            reader.toggleBookmark();
            QVERIFY(reader.bookmarked());
            QVERIFY(reader.notesMarkdown().contains("> Chapter One"));
            QVERIFY(reader.notesMarkdown().contains("A note."));

            const QString markdown = dir.path() + "/notes.md";
            QVERIFY(reader.exportNotes(QUrl::fromLocalFile(markdown)));
            QFile file(markdown);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY(file.readAll().startsWith("# The Test Book"));
        }
        Reader reader(&library, &preferences);
        QVERIFY(reader.openBook(id));
        reader.setGeometry(400, 500, 1, 0);
        QCOMPARE(reader.highlights().size(), 1);
        QCOMPARE(reader.bookmarks().size(), 1);
        reader.removeHighlight(reader.highlights()[0].toMap()["id"].toString());
        QVERIFY(reader.highlights().isEmpty());
        reader.undo();
        QCOMPARE(reader.highlights().size(), 1);
    }

    void readerSearchesTheBook()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        Reader reader(&library, &preferences);
        QVERIFY(reader.openBook(library.add(makeEpub(dir.path()))));
        reader.setGeometry(400, 500, 1, 0);
        reader.search("marmalade");
        QCOMPARE(reader.searchResults().size(), 2);
        const auto first = reader.searchResults()[0].toMap();
        QCOMPARE(first["match"].toString().toLower(), QString("marmalade"));
        reader.goToResult(1);
        QVERIFY(reader.page() > 0);
        reader.search("x");
        QVERIFY(reader.searchResults().isEmpty());
    }

    void readerOpensPdfs()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString id = library.add(makePdf(dir.path() + "/paper.pdf", 3));
        QVERIFY2(!id.isEmpty(), qPrintable(library.error()));
        Reader reader(&library, &preferences);
        QVERIFY2(reader.openBook(id), qPrintable(reader.error()));
        reader.setGeometry(600, 800, 1, 0);
        QCOMPARE(reader.pageCount(), 3);
        reader.next();
        QCOMPARE(reader.page(), 1);
        reader.search("page three");
        QCOMPARE(reader.searchResults().size(), 1);
        reader.goToResult(0);
        QCOMPARE(reader.page(), 2);
        reader.select(0, 0, 1);
        QCOMPARE(reader.selectionText(), QString("This is"));
        QVERIFY(!reader.highlight("yellow").isEmpty());
        QCOMPARE(reader.highlights()[0].toMap()["page"].toInt(), 3);

        QImage image(600, 800, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        reader.paint(&painter, image.size());
        painter.end();
        int inked = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                inked += image.pixelColor(x, y) != QColor(Qt::white);
        QVERIFY(inked > 100);
    }

    void smallPrintPdfsFitTextToSize()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString small = library.add(makeTextPdf(dir.path() + "/small.pdf", 7, 2));
        const QString large = library.add(makeTextPdf(dir.path() + "/large.pdf", 20, 2));
        QVERIFY2(!small.isEmpty() && !large.isEmpty(), qPrintable(library.error()));
        Reader reader(&library, &preferences);

        // The same landscape window: one book is small print in it, the other isn't.
        QVERIFY(reader.openBook(large));
        reader.setGeometry(1200, 650, 1, 0);
        // Poppler's word boxes are the font's full height: the size, or a little more.
        QVERIFY2(reader.bodySize() >= 20 && reader.bodySize() <= 25, qPrintable(QString::number(reader.bodySize())));
        QVERIFY(!reader.smallPrint());
        QVERIFY(!reader.fitText());
        QCOMPARE(reader.sliceCount(), 1);

        QVERIFY(reader.openBook(small));
        reader.setGeometry(1200, 650, 1, 0);
        QVERIFY2(reader.bodySize() >= 7 && reader.bodySize() <= 9, qPrintable(QString::number(reader.bodySize())));
        QVERIFY(reader.smallPrint());
        QVERIFY(reader.fitText());
        const int slices = reader.sliceCount();
        QVERIFY2(slices >= 3, qPrintable(QString::number(slices)));

        // Turning moves down the page first, then on to the next page, and
        // back from the top of a page lands at the foot of the one before.
        reader.next();
        QCOMPARE(reader.page(), 0);
        QCOMPARE(reader.slice(), 1);
        for (int i = 1; i < slices; ++i)
            reader.next();
        QCOMPARE(reader.page(), 1);
        QCOMPARE(reader.slice(), 0);
        reader.previous();
        QCOMPARE(reader.page(), 0);
        QCOMPARE(reader.slice(), slices - 1);
        QVERIFY(!reader.atEnd());
        reader.goToEnd();
        QCOMPARE(reader.page(), 1);
        QCOMPARE(reader.slice(), slices - 1);
        QVERIFY(reader.atEnd());

        // Each slice shows text, and the zoom makes it the reader's size:
        // a line of 7 pt text at 20 px is about three times as tall.
        reader.goToPage(0);
        reader.next();
        QImage image(1200, 650, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        reader.paint(&painter, image.size());
        painter.end();
        int inked = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                inked += image.pixelColor(x, y) != QColor(Qt::white);
        QVERIFY(inked > 1000);

        // The place is the slice, kept when the book is opened again.
        reader.next();
        QCOMPARE(reader.slice(), 2);
        reader.close();
        QVERIFY(reader.openBook(small));
        reader.setGeometry(1200, 650, 1, 0);
        QCOMPARE(reader.page(), 0);
        QCOMPARE(reader.slice(), 2);

        // Bigger text means more slices.
        preferences.setFontSize(30);
        QVERIFY(reader.sliceCount() > slices);
        preferences.setFontSize(20);

        // Turning it off is remembered for the book; turning it back on
        // follows detection again.
        reader.setFitText(false);
        QVERIFY(!reader.fitText());
        QCOMPARE(reader.sliceCount(), 1);
        reader.close();
        QVERIFY(!library.annotations(small)["fitText"].toBool(true));
        QVERIFY(reader.openBook(small));
        reader.setGeometry(1200, 650, 1, 0);
        QVERIFY(!reader.fitText());
        reader.setFitText(true);
        QVERIFY(reader.fitText());
        reader.close();
        QVERIFY(!library.annotations(small).contains("fitText"));
    }

    void fitTextLeavesOutRunningHeadsAndFeet()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString id = library.add(makeTextPdf(dir.path() + "/guide.pdf", 7, 4, true));
        QVERIFY2(!id.isEmpty(), qPrintable(library.error()));
        Reader reader(&library, &preferences);
        QVERIFY(reader.openBook(id));
        reader.setGeometry(1200, 650, 1, 0);
        QVERIFY(reader.fitText());

        // The word nearest the left of the view's top or bottom edge, where the
        // running head ("Study guide") and foot ("Page 1") start.
        auto wordAt = [&](double y) {
            const int word = reader.positionAt(0, QPointF(0, y));
            reader.select(0, word, word);
            const QString text = reader.selectionText();
            reader.clearSelection();
            return text;
        };
        QCOMPARE(wordAt(0), QString("Line"));
        while (reader.page() == 0 && !reader.atEnd() && reader.slice() + 1 < reader.sliceCount())
            reader.next();
        QCOMPARE(reader.page(), 0);
        QCOMPARE(wordAt(649), QString("Line"));
    }

    void quireDataMovesToNell()
    {
        QTemporaryDir dir;
        const QString data = dir.path() + "/data", config = dir.path() + "/config", cache = dir.path() + "/cache";
        QDir().mkpath(data + "/quire/books");
        QDir().mkpath(config + "/quire");
        QDir().mkpath(cache + "/quire/covers");
        const QString cover = cache + "/quire/covers/abc.jpg";
        QFile(cover).open(QIODevice::WriteOnly);
        QFile(config + "/quire/quire.conf").open(QIODevice::WriteOnly);
        QFile(data + "/quire/books/abc.json").open(QIODevice::WriteOnly);
        QVERIFY(Library::writeJson(data + "/quire/library.json",
                                   QJsonObject{{"version", 1}, {"books", QJsonArray{QJsonObject{{"id", "abc"}, {"cover", cover}}}}}));

        Library::adoptQuire(data, config, cache);
        QVERIFY(!QDir(data + "/quire").exists() && !QDir(config + "/quire").exists() && !QDir(cache + "/quire").exists());
        QVERIFY(QFile::exists(data + "/nell/books/abc.json"));
        QVERIFY(QFile::exists(config + "/nell/nell.conf"));
        Library library(data + "/nell", cache + "/nell");
        const QString moved = library.books().value(0).toMap()["cover"].toString();
        QCOMPARE(moved, cache + "/nell/covers/abc.jpg");
        QVERIFY(QFile::exists(moved));

        // Once Nell has its own, a leftover Quire directory is left alone.
        QDir().mkpath(data + "/quire");
        Library::adoptQuire(data, config, cache);
        QVERIFY(QDir(data + "/quire").exists());
        QVERIFY(QFile::exists(data + "/nell/library.json"));
    }

    void readingSpeedIsLearnedSlowly()
    {
        QTemporaryDir dir;
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const double start = preferences.readingSpeed();
        preferences.learnReadingSpeed(start * 10);
        QVERIFY(preferences.readingSpeed() > start);
        QVERIFY(preferences.readingSpeed() < start * 3);
        preferences.learnReadingSpeed(100000);
        QVERIFY(preferences.readingSpeed() <= 200);
        Preferences again(nullptr, dir.path() + "/nell.conf");
        QCOMPARE(again.readingSpeed(), preferences.readingSpeed());
    }

    void readingThemesHaveColors()
    {
        QTemporaryDir dir;
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        QCOMPARE(preferences.pageColor(), QString("#ffffff"));
        QVERIFY(!preferences.darkPage());
        preferences.setReadingTheme("night");
        QVERIFY(preferences.darkPage());
        preferences.setReadingTheme("nonsense");
        QCOMPARE(preferences.readingTheme(), QString("night"));
    }

    void dictionaryParsesWiktionary()
    {
        QCOMPARE(Dictionary::cleanWord("“Alice’s,”"), QString("Alice"));
        QCOMPARE(Dictionary::cleanWord("  hurried. "), QString("hurried"));
        const QByteArray json = R"({"en": [{"partOfSpeech": "Noun", "definitions": [{"definition": "A <b>small</b> fruit."}]}],
                                   "fr": [{"partOfSpeech": "Nom", "definitions": [{"definition": "Un fruit."}]}]})";
        const auto english = Dictionary::parse(json, "en");
        QCOMPARE(english.size(), 1);
        QCOMPARE(english[0].toMap()["definitions"].toStringList()[0], QString("A small fruit."));
        QCOMPARE(Dictionary::parse(json, "fr")[0].toMap()["partOfSpeech"].toString(), QString("Nom"));
        QCOMPARE(Dictionary::parse(json, "de")[0].toMap()["partOfSpeech"].toString(), QString("Noun"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    // A small EPUB 3 with a nav document, two chapters, an anchor, and a cover.
    static QString makeEpub(const QString &directory)
    {
        const QString root = directory + "/source";
        write(root + "/mimetype", "application/epub+zip");
        write(root + "/META-INF/container.xml", R"(<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>)");
        write(root + "/OEBPS/content.opf", R"(<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>The Test Book</dc:title><dc:creator>Ada Writer</dc:creator><dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="one" href="text/one.xhtml" media-type="application/xhtml+xml"/>
    <item id="two" href="text/two.xhtml" media-type="application/xhtml+xml"/>
    <item id="cover" href="cover.png" media-type="image/png" properties="cover-image"/>
    <item id="css" href="style.css" media-type="text/css"/>
  </manifest>
  <spine><itemref idref="one"/><itemref idref="two"/></spine>
</package>)");
        write(root + "/OEBPS/nav.xhtml", R"(<?xml version="1.0"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body>
<nav epub:type="toc"><ol>
  <li><a href="text/one.xhtml">Chapter One</a></li>
  <li><a href="text/two.xhtml">Chapter Two</a><ol><li><a href="text/two.xhtml#section">A Section</a></li></ol></li>
</ol></nav>
<nav epub:type="landmarks"><ol><li><a href="text/one.xhtml">Start</a></li></ol></nav>
</body></html>)");
        write(root + "/OEBPS/style.css", "p { color: red; font-family: Comic Sans; line-height: 3; text-indent: 1em }\n");
        QByteArray filler;
        for (int i = 0; i < 40; ++i)
            filler += "<p>Alice was beginning to get very tired of sitting by her sister on the bank, and of having "
                      "nothing to do: once or twice she had peeped into the book her sister was reading.</p>\n";
        auto chapter = [&](const QByteArray &title, const QByteArray &middle) {
            return "<?xml version=\"1.0\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\"><head>"
                   "<link rel=\"stylesheet\" href=\"../style.css\"/></head><body><h1>" + title + "</h1>" +
                   filler + middle + filler + "</body></html>";
        };
        write(root + "/OEBPS/text/one.xhtml", chapter("Chapter One", "<p>A jar of ORANGE MARMALADE.</p>"));
        write(root + "/OEBPS/text/two.xhtml",
              chapter("Chapter Two", "<h2 id=\"section\">A Section</h2><p>More marmalade here.</p>"));
        QImage cover(60, 90, QImage::Format_RGB32);
        cover.fill(Qt::darkBlue);
        cover.save(root + "/OEBPS/cover.png");

        const QString book = directory + "/book.epub";
        QProcess zip;
        zip.setWorkingDirectory(root);
        zip.start("bsdtar", {"--format", "zip", "-cf", book, "mimetype", "META-INF", "OEBPS"});
        if (!zip.waitForFinished() || zip.exitCode() != 0)
            qFatal("bsdtar failed: %s", zip.readAllStandardError().constData());
        return book;
    }

    // A4 pages filled top to bottom with lines of text at one size; with
    // running, the text keeps to the middle under a running head and above
    // a numbered running foot.
    static QString makeTextPdf(const QString &path, int pointSize, int pages, bool running = false)
    {
        QPdfWriter writer(path);
        writer.setTitle(QString());
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.setFont(QFont("sans", pointSize));
        const double line = writer.resolution() * pointSize * 1.4 / 72;
        for (int page = 0; page < pages; ++page) {
            if (page > 0)
                writer.newPage();
            const double top = running ? writer.height() * 0.16 : line * 2;
            const double bottom = running ? writer.height() * 0.84 : writer.height() - line;
            for (double y = top; y < bottom; y += line)
                painter.drawText(QPointF(line, y), QString("Line at %1 of page %2 with a few more words").arg(int(y)).arg(page + 1));
            if (running) {
                painter.drawText(QPointF(line, writer.height() * 0.05), "Study guide");
                painter.drawText(QPointF(line, writer.height() * 0.97), QString("Page %1").arg(page + 1));
            }
        }
        painter.end();
        return path;
    }

    static QString makePdf(const QString &path, int pages)
    {
        QPdfWriter writer(path);
        writer.setTitle(QString());
        QPainter painter(&writer);
        painter.setFont(QFont("sans", 14));
        const char *names[] = {"one", "two", "three", "four"};
        for (int page = 0; page < pages; ++page) {
            if (page > 0)
                writer.newPage();
            painter.drawText(QPointF(400, 800), QString("This is page %1 of the paper.").arg(names[page % 4]));
        }
        painter.end();
        return path;
    }
};

QTEST_MAIN(NellTests)
#include "nell_tests.moc"
