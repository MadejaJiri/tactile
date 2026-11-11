QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = tactile
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/clock.h \
    src/link.h \
    src/pressurechart.h \
    src/settings.h \
    src/tunes.h \
    src/theme.h \
    src/translator.h \
    src/version.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/link.cpp \
    src/pressurechart.cpp \
    src/settings.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
