#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>
#include <poppler-qt6.h>

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

        // Turning it off is remembered for the book; so is turning it on,
        // even though detection would have, and Auto follows detection again.
        QCOMPARE(reader.fitTextMode(), QString("auto"));
        reader.setFitText(false);
        QVERIFY(!reader.fitText());
        QCOMPARE(reader.fitTextMode(), QString("off"));
        QCOMPARE(reader.sliceCount(), 1);
        reader.close();
        QVERIFY(!library.annotations(small).object["fitText"].toBool(true));
        QVERIFY(reader.openBook(small));
        reader.setGeometry(1200, 650, 1, 0);
        QVERIFY(!reader.fitText());
        reader.setFitText(true);
        QVERIFY(reader.fitText());
        QCOMPARE(reader.fitTextMode(), QString("on"));
        reader.close();
        QVERIFY(library.annotations(small).object["fitText"].toBool(false));
        QVERIFY(reader.openBook(small));
        reader.setGeometry(1200, 650, 1, 0);
        QCOMPARE(reader.fitTextMode(), QString("on"));
        reader.setFitTextMode("auto");
        QVERIFY(reader.fitText());
        reader.close();
        QVERIFY(!library.annotations(small).object.contains("fitText"));
    }

    void smallPrintIsMeasuredAtTheColumnShown()
    {
        // 10 pt text in a tall window: fine on one page, small print when
        // the window shows two pages side by side.
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString id = library.add(makeTextPdf(dir.path() + "/ten.pdf", 10, 2));
        Reader reader(&library, &preferences);
        QVERIFY(reader.openBook(id));
        reader.setGeometry(1200, 1600, 1, 0);
        QVERIFY(!reader.smallPrint());
        QVERIFY(!reader.fitText());
        reader.setGeometry(1200, 1600, 2, 20);
        QVERIFY(reader.smallPrint());
        // Resizes are paginated once they settle.
        QTRY_VERIFY(reader.fitText());
        QCOMPARE(reader.columns(), 1);
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

        // A heading as high as the running head, but only on its own page, stays.
        auto document = Poppler::Document::load(dir.path() + "/guide.pdf");
        double heading = -1;
        for (const auto &box : document->page(2)->textList())
            if (box->text() == QLatin1String("Chapter"))
                heading = box->boundingBox().top();
        QVERIFY(heading > 0);
        reader.goToPage(2);
        QVERIFY2(reader.visibleSpan().first <= heading, qPrintable(QString::number(reader.visibleSpan().first)));
        reader.goToPage(1);
        QVERIFY(reader.visibleSpan().first > heading);

        // Running heads can be shown again without turning fit text off.
        preferences.setHideRunningHeads(false);
        QVERIFY(reader.fitText());
        reader.goToPage(0);
        QCOMPARE(wordAt(0), QString("Study"));
    }

    void fitTextNeverLosesALine()
    {
        // Two columns whose lines are staggered by half a line: no cut across
        // the page misses every line, so slices must overlap instead.
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString path = makeStaggeredPdf(dir.path() + "/columns.pdf");
        const QString id = library.add(path);
        Reader reader(&library, &preferences);
        QVERIFY(reader.openBook(id));
        reader.setGeometry(1200, 650, 1, 0);
        QVERIFY(reader.fitText());
        QList<QPair<double, double>> spans{reader.visibleSpan()};
        while (reader.page() == 0 && reader.slice() + 1 < reader.sliceCount()) {
            reader.next();
            spans << reader.visibleSpan();
        }
        QVERIFY(spans.size() > 2);
        auto document = Poppler::Document::load(path);
        int words = 0;
        for (const auto &box : document->page(0)->textList()) {
            const QRectF rect = box->boundingBox();
            bool whole = false;
            for (const auto &span : spans)
                whole = whole || (rect.top() - rect.height() / 4 >= span.first - 0.01 && rect.bottom() <= span.second + 0.01);
            QVERIFY2(whole, qPrintable(QString("%1 at %2–%3").arg(box->text()).arg(rect.top()).arg(rect.bottom())));
            ++words;
        }
        QVERIFY(words > 100);
        // The place still finds its slice again.
        reader.goToPage(0);
        reader.next();
        reader.next();
        const int slice = reader.slice();
        reader.close();
        QVERIFY(reader.openBook(id));
        reader.setGeometry(1200, 650, 1, 0);
        QCOMPARE(reader.slice(), slice);
    }

    void damagedOrNewerNotesAreNeverOverwritten()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString id = library.add(makeEpub(dir.path()));
        const QString path = dir.path() + "/data/books/" + id + ".json";
        for (const QByteArray &contents : {QByteArray("{\"version\": 1, \"highlights\": [ oops"),
                                           QByteArray("{\"version\": 2, \"position\": {\"chapter\": 1}, \"pins\": []}")}) {
            write(path, contents);
            {
                Reader reader(&library, &preferences);
                QVERIFY(reader.openBook(id));
                reader.setGeometry(400, 500, 1, 0);
                QVERIFY(reader.notesLocked());
                reader.next();
                reader.toggleBookmark();
                reader.close();
            }
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), contents);
        }
        QCOMPARE(library.storageProblems().size(), 2);
    }

    void unknownFieldsSurviveASave()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString id = library.add(makeEpub(dir.path()));
        QVERIFY(Library::writeJson(dir.path() + "/data/books/" + id + ".json",
                                   QJsonObject{{"version", 1},
                                               {"pins", QJsonArray{1, 2}},
                                               {"highlights", QJsonArray{QJsonObject{{"id", "h1"}, {"chapter", 0}, {"start", 0},
                                                                                     {"end", 7}, {"color", "blue"}, {"stars", 3}}}}}));
        {
            Reader reader(&library, &preferences);
            QVERIFY(reader.openBook(id));
            reader.setGeometry(400, 500, 1, 0);
            QVERIFY(!reader.notesLocked());
            reader.toggleBookmark();
        }
        const QJsonObject saved = library.annotations(id).object;
        QCOMPARE(saved["pins"].toArray(), (QJsonArray{1, 2}));
        QCOMPARE(saved["highlights"].toArray()[0].toObject()["stars"].toInt(), 3);
        QCOMPARE(saved["bookmarks"].toArray().size(), 1);

        // The library keeps fields it doesn't know too.
        QJsonObject object = Library::readStore(dir.path() + "/data/library.json").object;
        object["shelves"] = QJsonArray{"Read later"};
        QVERIFY(Library::writeJson(dir.path() + "/data/library.json", object));
        Library again(dir.path() + "/data", dir.path() + "/cache");
        QVERIFY(!again.add(makePdf(dir.path() + "/paper.pdf", 1)).isEmpty());
        QCOMPARE(Library::readStore(dir.path() + "/data/library.json").object["shelves"].toArray(), (QJsonArray{"Read later"}));
    }

    void damagedOrNewerLibraryIsOnlyRead()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/data/library.json";
        const QString pdf = makePdf(dir.path() + "/paper.pdf", 1);
        for (const QByteArray &contents :
             {QByteArray("{\"version\": 1, \"books\": [{\"id\": \"abc\""),
              QByteArray("{\"version\": 2, \"books\": [{\"id\": \"abc\", \"title\": \"Kept\"}]}")}) {
            write(path, contents);
            Library library(dir.path() + "/data", dir.path() + "/cache");
            QCOMPARE(library.storageProblems().size(), 1);
            QVERIFY(library.add(pdf).isEmpty());
            QVERIFY(!library.error().isEmpty());
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), contents);
        }
        // A newer library is still shown.
        Library library(dir.path() + "/data", dir.path() + "/cache");
        QCOMPARE(library.books().value(0).toMap()["title"].toString(), QString("Kept"));
    }

    void damagedOrNewerSettingsAreOnlyRead()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/nell.conf";
        for (const QByteArray &contents : {QByteArray("[reading\nfontSize=24\n"),
                                           QByteArray("[General]\nversion=2\n\n[reading]\nfontSize=24\n")}) {
            write(path, contents);
            {
                Preferences preferences(nullptr, path);
                QVERIFY(!preferences.storageProblem().isEmpty());
                preferences.setFontSize(30);
                preferences.learnReadingSpeed(50);
            }
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), contents);
        }
        write(path, "[reading]\nfontSize=24\n");
        Preferences preferences(nullptr, path);
        QVERIFY(preferences.storageProblem().isEmpty());
        QCOMPARE(preferences.fontSize(), 24);
    }

    void notesThatFailToSaveAreKept()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString id = library.add(makeEpub(dir.path()));
        const QString books = dir.path() + "/data/books";
        QVERIFY(QDir().mkpath(books));
        Reader reader(&library, &preferences);
        QVERIFY(reader.openBook(id));
        reader.setGeometry(400, 500, 1, 0);
        QVERIFY(QFile::setPermissions(books, QFile::ReadOwner | QFile::ExeOwner));
        reader.toggleBookmark();
        reader.close();
        QVERIFY(!QFile::exists(books + "/" + id + ".json"));
        QVERIFY(!library.error().isEmpty());

        // Opened again, the bookmark is there, and saved once it can be.
        QVERIFY(QFile::setPermissions(books, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(reader.openBook(id));
        reader.setGeometry(400, 500, 1, 0);
        QCOMPARE(reader.bookmarks().size(), 1);
        reader.close();
        QCOMPARE(library.annotations(id).object["bookmarks"].toArray().size(), 1);
    }

    void twoLibrariesKeepEachOthersBooks()
    {
        // A window and `nell add`, each with the library as it was when it started.
        QTemporaryDir dir;
        Library window(dir.path() + "/data", dir.path() + "/cache");
        Library command(dir.path() + "/data", dir.path() + "/cache");
        const QString epub = window.add(makeEpub(dir.path()));
        const QString pdf = command.add(makePdf(dir.path() + "/paper.pdf", 1));
        QVERIFY(!epub.isEmpty() && !pdf.isEmpty());
        window.touch(epub, 0.5);
        Library after(dir.path() + "/data", dir.path() + "/cache");
        QCOMPARE(after.books().size(), 2);
        QCOMPARE(after.book(epub)["progress"].toDouble(), 0.5);
        QCOMPARE(window.books().size(), 2);
        command.remove(epub);
        QCOMPARE(Library(dir.path() + "/data", dir.path() + "/cache").books().size(), 1);
    }

    void twoReadersMergeTheirNotes()
    {
        QTemporaryDir dir;
        Library one(dir.path() + "/data", dir.path() + "/cache");
        const QString id = one.add(makeEpub(dir.path()));
        Library two(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        Reader first(&one, &preferences), second(&two, &preferences);
        QVERIFY(first.openBook(id) && second.openBook(id));
        first.setGeometry(400, 500, 1, 0);
        second.setGeometry(400, 500, 1, 0);
        first.select(0, 2, 9);
        const QString kept = first.highlight("green");
        second.toggleBookmark();
        first.close();
        second.close();
        QJsonObject saved = one.annotations(id).object;
        QCOMPARE(saved["highlights"].toArray().size(), 1);
        QCOMPARE(saved["bookmarks"].toArray().size(), 1);

        // One removes a highlight while the other adds one: both stand.
        QVERIFY(first.openBook(id) && second.openBook(id));
        first.setGeometry(400, 500, 1, 0);
        second.setGeometry(400, 500, 1, 0);
        first.removeHighlight(kept);
        second.select(0, 20, 30);
        const QString added = second.highlight("blue");
        first.close();
        second.close();
        saved = one.annotations(id).object;
        QCOMPARE(saved["highlights"].toArray().size(), 1);
        QCOMPARE(saved["highlights"].toArray()[0].toObject()["id"].toString(), added);
        QCOMPARE(saved["bookmarks"].toArray().size(), 1);
    }

    void aBookChangedInPlaceKeepsItsOldNotes()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/data", dir.path() + "/cache");
        Preferences preferences(nullptr, dir.path() + "/nell.conf");
        const QString path = makePdf(dir.path() + "/paper.pdf", 3);
        const QString id = library.add(path);
        QCOMPARE(library.book(id)["sha256"].toString().size(), 64);
        Reader reader(&library, &preferences);
        QVERIFY(reader.openBook(id));
        reader.setGeometry(600, 800, 1, 0);
        reader.select(0, 0, 1);
        QVERIFY(!reader.highlight("yellow").isEmpty());
        reader.close();
        const QByteArray notes = [&] {
            QFile file(dir.path() + "/data/books/" + id + ".json");
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        }();

        // Touched but the same: the same book, and its notes.
        QFile touched(path);
        QVERIFY(touched.open(QIODevice::ReadWrite));
        QVERIFY(touched.setFileTime(QDateTime::currentDateTime().addDays(1), QFileDevice::FileModificationTime));
        touched.close();
        QVERIFY(reader.openBook(id));
        QCOMPARE(reader.bookId(), id);
        QCOMPARE(reader.highlights().size(), 1);
        reader.close();

        // Different contents under the same name: a new revision, with no
        // notes, and the old notes left exactly where they were.
        QVERIFY(QFile::remove(path));
        makePdf(path, 2);
        QSignalSpy changed(&reader, &Reader::fileChanged);
        QVERIFY(reader.openBook(id));
        QVERIFY(reader.bookId() != id);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(reader.previousRevision(), id);
        QVERIFY(reader.highlights().isEmpty());
        QCOMPARE(reader.pageCount(), 2);
        reader.close();
        QFile file(dir.path() + "/data/books/" + id + ".json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), notes);
        QCOMPARE(library.books().size(), 2);
        QCOMPARE(library.book(id)["replacedBy"].toString(), changed[0][1].toString());
    }

    void epubCacheIsCheckedAgainstTheFile()
    {
        QTemporaryDir dir;
        const QString first = makeEpub(dir.path() + "/a", "First Title");
        const QString second = makeEpub(dir.path() + "/b", "Second Title");
        QString error;
        Epub epub;
        QVERIFY(Epub::extract(first, dir.path() + "/unpacked", &error, Library::fileDigest(first)));
        QVERIFY(epub.load(dir.path() + "/unpacked", &error));
        QCOMPARE(epub.title, QString("First Title"));
        // An unpacked book from another file isn't reused.
        QVERIFY(Epub::extract(second, dir.path() + "/unpacked", &error, Library::fileDigest(second)));
        Epub again;
        QVERIFY(again.load(dir.path() + "/unpacked", &error));
        QCOMPARE(again.title, QString("Second Title"));
    }

    void quireDataMovesToNell()
    {
        QTemporaryDir dir;
        const QString data = dir.path() + "/data", config = dir.path() + "/config", cache = dir.path() + "/cache";
        const QString cover = makeQuire(dir.path());

        QVERIFY(Library::adoptQuire(data, config, cache).isEmpty());
        QVERIFY(!QDir(data + "/quire").exists() && !QDir(config + "/quire").exists() && !QDir(cache + "/quire").exists());
        QVERIFY(QFile::exists(data + "/nell/books/abc.json"));
        QVERIFY(QFile::exists(config + "/nell/nell.conf"));
        Library library(data + "/nell", cache + "/nell");
        const QString moved = library.books().value(0).toMap()["cover"].toString();
        QCOMPARE(moved, cache + "/nell/covers/abc.jpg");
        QVERIFY(QFile::exists(moved));

        // Once Nell has its own, a leftover Quire directory is left alone.
        QDir().mkpath(data + "/quire");
        QVERIFY(Library::adoptQuire(data, config, cache).isEmpty());
        QVERIFY(QDir(data + "/quire").exists());
        QVERIFY(QFile::exists(data + "/nell/library.json"));
        QVERIFY(!Library::quireWaiting(data));
    }

    void quireAdoptionResumes()
    {
        QTemporaryDir dir;
        const QString data = dir.path() + "/data", config = dir.path() + "/config", cache = dir.path() + "/cache";
        makeQuire(dir.path());
        // An earlier run moved the settings directory but not the file in
        // it, and moved the cache but not the data; an empty nell data
        // directory was left behind.
        QVERIFY(QDir().rename(config + "/quire", config + "/nell"));
        QVERIFY(QDir().rename(cache + "/quire", cache + "/nell"));
        QVERIFY(QDir().mkpath(data + "/nell"));
        QVERIFY(Library::quireWaiting(data));

        QVERIFY(Library::adoptQuire(data, config, cache).isEmpty());
        QVERIFY(!QDir(data + "/quire").exists());
        QVERIFY(QFile::exists(data + "/nell/books/abc.json"));
        QVERIFY(QFile::exists(config + "/nell/nell.conf"));
        QVERIFY(!QFile::exists(config + "/nell/quire.conf"));
        Library library(data + "/nell", cache + "/nell");
        QCOMPARE(library.books().value(0).toMap()["cover"].toString(), cache + "/nell/covers/abc.jpg");

        // A cache Nell already had gains Quire's covers but is otherwise its own.
        QTemporaryDir second;
        const QString data2 = second.path() + "/data", config2 = second.path() + "/config", cache2 = second.path() + "/cache";
        makeQuire(second.path());
        write(cache2 + "/nell/other/thing", "x");
        QVERIFY(Library::adoptQuire(data2, config2, cache2).isEmpty());
        QVERIFY(QFile::exists(cache2 + "/nell/covers/abc.jpg"));
        QVERIFY(QFile::exists(cache2 + "/nell/other/thing"));
        Library library2(data2 + "/nell", cache2 + "/nell");
        QCOMPARE(library2.books().value(0).toMap()["cover"].toString(), cache2 + "/nell/covers/abc.jpg");
    }

    void startupLeavesStorageAloneUntilNeeded()
    {
        QTemporaryDir dir;
        makeQuire(dir.path());
        auto quireThere = [&] {
            return QDir(dir.path() + "/data/quire").exists() && QDir(dir.path() + "/config/quire").exists() &&
                   QDir(dir.path() + "/cache/quire").exists() && !QFileInfo::exists(dir.path() + "/data/nell") &&
                   !QFileInfo::exists(dir.path() + "/config/nell") && !QFileInfo::exists(dir.path() + "/cache/nell");
        };

        auto version = runNell(dir.path(), {"--version"});
        QCOMPARE(version.code, 0);
        QVERIFY2(QRegularExpression(R"(^nell \d+\.\d+\.\d+\n$)").match(QString::fromUtf8(version.out)).hasMatch(),
                 version.out.constData());
        QVERIFY(quireThere());
        QCOMPARE(runNell(dir.path(), {"--help"}).code, 0);
        QVERIFY(runNell(dir.path(), {"--no-such-option"}).code != 0);
        QCOMPARE(runNell(dir.path(), {"open", "--version"}).code, 0);
        QCOMPARE(runNell(dir.path(), {"add", "--help"}).code, 0);
        auto list = runNell(dir.path(), {"list"});
        QCOMPARE(list.code, 0);
        QVERIFY2(list.err.contains("hasn't been moved"), list.err.constData());
        QVERIFY(runNell(dir.path(), {"notes", "abc"}).code != 0);
        QVERIFY(quireThere());

        // Adding a book is a write, so Quire's library comes across first
        // and the new book joins it.
        auto add = runNell(dir.path(), {"add", makePdf(dir.path() + "/paper.pdf", 1)});
        QVERIFY2(add.code == 0, add.err.constData());
        QVERIFY(!QDir(dir.path() + "/data/quire").exists());
        QVERIFY(QFile::exists(dir.path() + "/config/nell/nell.conf"));
        Library library(dir.path() + "/data/nell", dir.path() + "/cache/nell");
        QCOMPARE(library.books().size(), 2);
    }

    void windowStartsAndAdoptsQuire()
    {
        QTemporaryDir dir;
        makeQuire(dir.path());
        QProcess nell;
        nell.setProcessEnvironment(nellEnvironment(dir.path()));
        nell.start(nellBinary(), {});
        QVERIFY2(nell.waitForStarted(), qPrintable(nell.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dir.path() + "/data/nell/library.json"), 15000);
        // The window loads and stays up: QML that fails to load exits at once.
        QTest::qWait(1500);
        QVERIFY2(nell.state() == QProcess::Running, nell.readAllStandardError().constData());
        nell.terminate();
        if (!nell.waitForFinished(5000))
            nell.kill();
        QVERIFY(!QDir(dir.path() + "/data/quire").exists());
        QVERIFY(QFile::exists(dir.path() + "/config/nell/nell.conf"));
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
    struct Run {
        int code = -1;
        QByteArray out, err;
    };

    static QString nellBinary()
    {
        const QString binary = qEnvironmentVariable("NELL_BINARY", QCoreApplication::applicationDirPath() + "/../nell");
        if (!QFileInfo(binary).isExecutable())
            qFatal("No nell binary at %s; run bin/test, which builds it.", qPrintable(binary));
        return binary;
    }

    // Everything the app could touch, inside home: never the real storage.
    static QProcessEnvironment nellEnvironment(const QString &home)
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("HOME", home);
        environment.insert("XDG_DATA_HOME", home + "/data");
        environment.insert("XDG_CONFIG_HOME", home + "/config");
        environment.insert("XDG_CACHE_HOME", home + "/cache");
        environment.insert("XDG_STATE_HOME", home + "/state");
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        return environment;
    }

    static Run runNell(const QString &home, const QStringList &arguments)
    {
        QProcess nell;
        nell.setProcessEnvironment(nellEnvironment(home));
        nell.start(nellBinary(), arguments);
        Run run;
        if (!nell.waitForFinished(30000)) {
            nell.kill();
            nell.waitForFinished();
            return run;
        }
        run.code = nell.exitStatus() == QProcess::NormalExit ? nell.exitCode() : -1;
        run.out = nell.readAllStandardOutput();
        run.err = nell.readAllStandardError();
        return run;
    }

    // Quire's data, settings, and cache under root, as 0.2.0 left them, with
    // one book whose cover is in the cache. Returns the cover's path.
    static QString makeQuire(const QString &root)
    {
        const QString data = root + "/data", config = root + "/config", cache = root + "/cache";
        const QString cover = cache + "/quire/covers/abc.jpg";
        write(cover, "jpeg");
        write(config + "/quire/quire.conf", "[reading]\nfontSize=24\n");
        write(data + "/quire/books/abc.json", "{\"version\": 1}");
        Library::writeJson(data + "/quire/library.json",
                           QJsonObject{{"version", 1}, {"books", QJsonArray{QJsonObject{{"id", "abc"}, {"cover", cover}}}}});
        return cover;
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    // A small EPUB 3 with a nav document, two chapters, an anchor, and a cover.
    static QString makeEpub(const QString &directory, const QByteArray &title = "The Test Book")
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
    <dc:title>)" + title + R"(</dc:title><dc:creator>Ada Writer</dc:creator><dc:language>en</dc:language>
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
                if (page == 2)
                    painter.drawText(QPointF(writer.width() / 2, writer.height() * 0.05), "Chapter Three");
            }
        }
        painter.end();
        return path;
    }

    // One A4 page of 7 pt text in two columns, the right one's lines half a
    // line lower than the left's.
    static QString makeStaggeredPdf(const QString &path)
    {
        QPdfWriter writer(path);
        writer.setTitle(QString());
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.setFont(QFont("sans", 7));
        const double line = writer.resolution() * 7 * 1.4 / 72;
        for (double y = line * 2; y < writer.height() - line * 2; y += line) {
            painter.drawText(QPointF(line, y), "Left column words here");
            painter.drawText(QPointF(writer.width() / 2, y + line / 2), "Right column words too");
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
