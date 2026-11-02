QT += core gui quick quickcontrols2 testlib dbus network
CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += poppler-qt6
TARGET = nell_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/dictionary.h ../src/epub.h ../src/hyphenator.h ../src/library.h ../src/preferences.h ../src/reader.h ../src/record.h \
    ../src/theme.h ../src/translator.h
SOURCES += nell_tests.cpp ../src/dictionary.cpp ../src/epub.cpp ../src/hyphenator.cpp ../src/library.cpp ../src/preferences.cpp \
    ../src/reader.cpp ../src/record.cpp ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
