#include "cli.h"

#include "epub.h"
#include "library.h"
#include "preferences.h"
#include "reader.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

namespace {
void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonValue &value) {
    const QJsonDocument document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    fprintf(stdout, "%s", document.toJson().constData());
}
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

struct Command {
    Command(const QString &name, const QString &description) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addOption({"json", "Print JSON"});
        this->name = name;
    }
    bool parse(const QStringList &arguments) {
        QStringList rest = arguments;
        rest.removeAt(1);
        rest[0] = "quire " + name;
        return parser.parse(rest);
    }
    QString name;
    QCommandLineParser parser;
};

QString percent(double progress) { return QString::number(qRound(progress * 100)) + "%"; }

int list(const QStringList &arguments) {
    Command command("list", "List the books in the library, most recently read first.");
    if (!command.parse(arguments))
        return fail(command.parser.errorText());
    if (command.parser.isSet("help"))
        return print(stdout, command.parser.helpText()), 0;
    Library library;
    const QVariantList books = library.sorted("recent", {});
    if (command.parser.isSet("json")) {
        print(QJsonArray::fromVariantList(books));
        return 0;
    }
    for (const auto &value : books) {
        const auto book = value.toMap();
        print(stdout, QString("%1  %2  %3%4%5")
                          .arg(book["id"].toString(), percent(book["progress"].toDouble()).rightJustified(4),
                               book["title"].toString(),
                               book["author"].toString().isEmpty() ? QString() : " — " + book["author"].toString(),
                               book["missing"].toBool() ? "  (missing)" : ""));
    }
    return 0;
}

int add(const QStringList &arguments) {
    Command command("add", "Add EPUB and PDF books to the library. The files stay where they are.");
    command.parser.addPositionalArgument("files", "Books to add", "<file>...");
    if (!command.parse(arguments))
        return fail(command.parser.errorText());
    if (command.parser.isSet("help"))
        return print(stdout, command.parser.helpText()), 0;
    const QStringList files = command.parser.positionalArguments();
    if (files.isEmpty())
        return fail("Name at least one EPUB or PDF file to add.");
    Library library;
    QJsonArray added;
    bool ok = true;
    for (const auto &file : files) {
        const QString id = library.add(file);
        if (id.isEmpty()) {
            print(stderr, file + ": " + library.error());
            ok = false;
            continue;
        }
        const auto book = QJsonObject::fromVariantMap(library.book(id));
        added << book;
        if (!command.parser.isSet("json"))
            print(stdout, QString("%1  %2").arg(id, book["title"].toString()));
    }
    if (command.parser.isSet("json"))
        print(added);
    return ok ? 0 : 1;
}

int info(const QStringList &arguments) {
    Command command("info", "Print a book's title, author, and table of contents.");
    command.parser.addPositionalArgument("book", "A book file, or the id of one in the library", "<book>");
    if (!command.parse(arguments))
        return fail(command.parser.errorText());
    if (command.parser.isSet("help"))
        return print(stdout, command.parser.helpText()), 0;
    if (command.parser.positionalArguments().size() != 1)
        return fail("Name one book file or library id.");
    const QString target = command.parser.positionalArguments().first();
    Library library;
    QString id = library.findId(target);
    BookInfo book;
    QString error;
    const QString path = id.isEmpty() ? target : library.book(id)["path"].toString();
    if (!Library::inspect(path, library.cacheRoot(), &book, &error))
        return fail(error);
    QJsonArray toc;
    if (book.format == QLatin1String("epub")) {
        Epub epub;
        if (epub.load(library.cacheDirectory(book.id), &error))
            for (const auto &entry : epub.toc)
                toc << QJsonObject{{"title", entry.title}, {"level", entry.level}};
    }
    const QJsonObject object{{"id", book.id},         {"path", book.path},
                             {"format", book.format}, {"title", book.title},
                             {"author", book.author}, {"language", book.language},
                             {"pages", book.pages ? QJsonValue(book.pages) : QJsonValue()},
                             {"inLibrary", !library.findId(book.id).isEmpty()},
                             {"toc", toc}};
    if (command.parser.isSet("json")) {
        print(object);
        return 0;
    }
    print(stdout, book.title + (book.author.isEmpty() ? QString() : " — " + book.author));
    print(stdout, QString("%1, id %2%3").arg(book.format.toUpper(), book.id,
                                             book.pages ? QString(", %1 pages").arg(book.pages) : QString()));
    for (const auto &entry : toc)
        print(stdout, QString(entry.toObject()["level"].toInt() * 2 + 2, ' ') + entry.toObject()["title"].toString());
    return 0;
}

int notes(const QStringList &arguments) {
    Command command("notes", "Print a book's highlights, notes, bookmarks, and looked-up words.");
    command.parser.addPositionalArgument("book", "A book in the library: its file or its id", "<book>");
    command.parser.addOption({"markdown", "Print Markdown, as Export Notes writes it"});
    if (!command.parse(arguments))
        return fail(command.parser.errorText());
    if (command.parser.isSet("help"))
        return print(stdout, command.parser.helpText()), 0;
    if (command.parser.positionalArguments().size() != 1)
        return fail("Name one book file or library id.");
    Library library;
    const QString target = command.parser.positionalArguments().first();
    QString id = library.findId(target);
    if (id.isEmpty() && QFileInfo(target).isFile())
        id = library.findId(Library::fileId(target));
    if (id.isEmpty())
        return fail(target + " isn't in the library. Add it with: quire add " + target);
    // Page numbers depend on layout; these are at a typical window size.
    Preferences preferences(nullptr);
    Reader reader(&library, &preferences);
    reader.setReadOnly(true);
    if (!reader.openBook(id))
        return fail(reader.error());
    reader.setGeometry(640, 900, 1, 0);
    if (command.parser.isSet("markdown")) {
        fprintf(stdout, "%s", qPrintable(reader.notesMarkdown()));
        return 0;
    }
    const QJsonObject object{{"id", id},
                             {"title", reader.title()},
                             {"author", reader.author()},
                             {"highlights", QJsonArray::fromVariantList(reader.highlights())},
                             {"bookmarks", QJsonArray::fromVariantList(reader.bookmarks())},
                             {"vocabulary", QJsonArray::fromVariantList(reader.vocabulary())}};
    if (command.parser.isSet("json")) {
        print(object);
        return 0;
    }
    fprintf(stdout, "%s", qPrintable(reader.notesMarkdown()));
    return 0;
}

int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.");
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/quire and link into ~/.claude/skills",
                                         "[install]");
    if (!command.parse(arguments))
        return fail(command.parser.errorText());
    if (command.parser.isSet("help"))
        return print(stdout, command.parser.helpText()), 0;
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return fail("The skill is missing from this build.");
    const QByteArray text = bundled.readAll();
    const QStringList positional = command.parser.positionalArguments();
    if (positional.isEmpty()) {
        fprintf(stdout, "%s", text.constData());
        return 0;
    }
    if (positional != QStringList{"install"})
        return fail("Use quire skill to print the skill, or quire skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/quire");
    if (!QDir().mkpath(directory))
        return fail("Could not create " + directory);
    QFile out(directory + "/SKILL.md");
    if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size())
        return fail("Could not write " + out.fileName());
    out.close();
    const QString link = home.filePath(".claude/skills/quire");
    if (!QFileInfo(link).isSymLink() && !QFileInfo::exists(link)) {
        if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/quire", link))
            return fail("Could not link " + link);
    }
    if (command.parser.isSet("json"))
        print(QJsonObject{{"installed", directory + "/SKILL.md"}, {"link", link}});
    else
        print(stdout, "Installed " + directory + "/SKILL.md, linked from " + link);
    return 0;
}
}

QString cliSummary() {
    return "Commands:\n"
           "  open [book]                     Open the reader, or a book in it (the default)\n"
           "  list                            List the library, most recently read first\n"
           "  add <file>...                   Add EPUB and PDF books to the library\n"
           "  info <book>                     Print a book's title, author, and contents\n"
           "  notes <book> [--markdown]       Print a book's highlights, notes, and words\n"
           "  skill [install]                 Print the skill for coding agents, or install it\n"
           "Every command takes --json and --help.";
}

bool isCliCommand(const QString &word) {
    return QStringList{"list", "add", "info", "notes", "skill", "help", "--help", "-h"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "list"    ? list(arguments)
           : command == "add"   ? add(arguments)
           : command == "info"  ? info(arguments)
           : command == "notes" ? notes(arguments)
           : command == "skill" ? skill(arguments)
                                : (print(stdout, "Usage: quire [command] [book]\n\n" + cliSummary()), 0);
}
