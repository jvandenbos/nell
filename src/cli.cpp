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
#include <QStandardPaths>
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

// Commands that only read leave Quire's library where it is, but say why
// they don't see it.
void noteQuire() {
    if (Library::quireWaiting(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)))
        print(stderr, "Quire's library hasn't been moved to Nell yet; it moves the first time Nell opens or adds a book.");
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
        rest[0] = "nell " + name;
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
    noteQuire();
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
    // Adding writes the library, so it's the one command that brings
    // Quire's across first; the others only read.
    adoptQuireStorage();
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
    noteQuire();
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
    noteQuire();
    Library library;
    const QString target = command.parser.positionalArguments().first();
    QString id = library.findId(target);
    if (id.isEmpty() && QFileInfo(target).isFile())
        id = library.findId(Library::fileId(target));
    if (id.isEmpty())
        return fail(target + " isn't in the library. Add it with: nell add " + target);
    // Places are pages and locations, which need no window; with the book
    // missing or changed, the notes still come from what's stored.
    Preferences preferences(nullptr);
    Reader reader(&library, &preferences);
    reader.setReadOnly(true);
    QJsonObject notes;
    if (reader.openBook(id)) {
        notes = reader.notesJson();
    } else {
        const Store store = library.annotations(id);
        if (store.state == StoreState::Corrupt)
            return fail(store.problem);
        print(stderr, reader.error() + " Showing the notes as saved.");
        notes = Reader::notesJson(library.book(id), store.object);
    }
    if (command.parser.isSet("json") && !command.parser.isSet("markdown")) {
        print(notes);
        return 0;
    }
    fprintf(stdout, "%s", qPrintable(Reader::notesMarkdown(notes)));
    return 0;
}

int importNotes(const QStringList &arguments) {
    Command command("import", "Read back notes exported with nell notes --json, merged with any already there.");
    command.parser.addPositionalArgument("file", "A JSON notes export", "<file>");
    if (!command.parse(arguments))
        return fail(command.parser.errorText());
    if (command.parser.isSet("help"))
        return print(stdout, command.parser.helpText()), 0;
    if (command.parser.positionalArguments().size() != 1)
        return fail("Name one exported notes file.");
    QFile file(command.parser.positionalArguments().first());
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.fileName() + ": " + file.errorString());
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return fail(file.fileName() + " isn't JSON: " + error.errorString());
    adoptQuireStorage();
    Library library;
    const QString id = library.importNotes(document.object());
    if (id.isEmpty())
        return fail(library.error());
    if (command.parser.isSet("json"))
        print(QJsonObject{{"id", id}, {"inLibrary", !library.book(id).isEmpty()}});
    else
        print(stdout, "Imported notes for " + id + (library.book(id).isEmpty() ? " (not in the library yet; add the book to see them)" : ""));
    return 0;
}

int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.");
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/nell and link into ~/.claude/skills",
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
        return fail("Use nell skill to print the skill, or nell skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/nell");
    if (!QDir().mkpath(directory))
        return fail("Could not create " + directory);
    QFile out(directory + "/SKILL.md");
    if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size())
        return fail("Could not write " + out.fileName());
    out.close();
    const QString link = home.filePath(".claude/skills/nell");
    if (!QFileInfo(link).isSymLink() && !QFileInfo::exists(link)) {
        if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/nell", link))
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
           "  import <file>                   Read back notes exported with notes --json\n"
           "  skill [install]                 Print the skill for coding agents, or install it\n"
           "Every command takes --json and --help.";
}

void adoptQuireStorage() {
    const QStringList problems =
        Library::adoptQuire(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
                            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
                            QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));
    for (const auto &problem : problems)
        print(stderr, "nell: " + problem);
}

bool isCliCommand(const QString &word) {
    return QStringList{"list", "add", "info", "notes", "import", "skill", "help", "--help", "-h"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "list"    ? list(arguments)
           : command == "add"   ? add(arguments)
           : command == "info"  ? info(arguments)
           : command == "notes" ? notes(arguments)
           : command == "import" ? importNotes(arguments)
           : command == "skill" ? skill(arguments)
                                : (print(stdout, "Usage: nell [command] [book]\n\n" + cliSummary()), 0);
}
