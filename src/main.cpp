// nell — reads EPUB and PDF books, after Apple Books, with the best of Kindle.

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QUrl>
#include <cstdio>

#include "cli.h"
#include "dictionary.h"
#include "library.h"
#include "pageview.h"
#include "portalfilepicker.h"
#include "preferences.h"
#include "reader.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Commands draw no window, so they must not need a display, even where
    // the desktop exports QT_QPA_PLATFORM=wayland.
    const bool command = argc > 1 && isCliCommand(QString::fromLocal8Bit(argv[1]));
    if (command)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("nell");
    app.setApplicationVersion(NELL_VERSION);
    Library::adoptQuire(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
                        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
                        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));

    // Matches the window to nell.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("nell");
    app.setWindowIcon(QIcon::fromTheme("nell"));

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    if (command)
        return runCli(app.arguments());

    QCommandLineParser args;
    args.setApplicationDescription("Reads EPUB and PDF books.\n\n" + cliSummary());
    args.addHelpOption();
    args.addVersionOption();
    args.addPositionalArgument("book", "An EPUB or PDF to add to the library and open", "[book]");
    QStringList arguments = app.arguments();
    if (arguments.value(1) == "open")
        arguments.removeAt(1);
    args.process(arguments);

    QQuickStyle::setStyle("Material");
    qmlRegisterType<PageView>("Nell", 1, 0, "PageView");
    qmlRegisterUncreatableType<Reader>("Nell", 1, 0, "Reader", "The reader is provided by the app");

    Theme theme;
    Preferences preferences(&theme);
    Library library;
    Reader reader(&library, &preferences);
    Dictionary dictionary;
    PortalFilePicker picker;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("prefs", &preferences);
    engine.rootContext()->setContextProperty("library", &library);
    engine.rootContext()->setContextProperty("reader", &reader);
    engine.rootContext()->setContextProperty("dictionary", &dictionary);
    engine.rootContext()->setContextProperty("picker", &picker);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Opening a file from the file manager or the command line adds it to
    // the library first, so its place and notes are kept like any other.
    const QStringList books = args.positionalArguments();
    if (!books.isEmpty()) {
        const QString id = library.add(books.first());
        if (!id.isEmpty())
            QMetaObject::invokeMethod(engine.rootObjects().first(), "openBook", Q_ARG(QVariant, id));
    }

    // Save the place before the window goes, not after the engine is gone.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &reader, &Reader::close);
    return app.exec();
}
