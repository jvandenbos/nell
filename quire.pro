QT += core gui qml quick quickcontrols2 dbus network

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += poppler-qt6
TARGET = quire
TEMPLATE = app

# The release version. pkgbuild/PKGBUILD and CHANGELOG.md must agree;
# bin/check-version (run by bin/test) fails when they drift. See docs/releasing.md.
VERSION = 0.1.0
DEFINES += QUIRE_VERSION=\\\"$$VERSION\\\"

HEADERS += \
    src/cli.h \
    src/dictionary.h \
    src/epub.h \
    src/filepicker.h \
    src/library.h \
    src/pageview.h \
    src/portalfilepicker.h \
    src/preferences.h \
    src/reader.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/cli.cpp \
    src/dictionary.cpp \
    src/epub.cpp \
    src/library.cpp \
    src/pageview.cpp \
    src/portalfilepicker.cpp \
    src/preferences.cpp \
    src/reader.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
