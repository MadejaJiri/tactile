QT += core gui qml quick quickcontrols2 network testlib

CONFIG += c++17 testcase
TARGET = tactile_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/daemon.h \
    ../src/driver.h \
    ../src/link.h \
    ../src/pressurechart.h \
    ../src/settings.h \
    ../src/theme.h \
    ../src/trackpad.h \
    ../src/translator.h

SOURCES += \
    tactile_tests.cpp \
    ../src/backend.cpp \
    ../src/daemon.cpp \
    ../src/driver.cpp \
    ../src/link.cpp \
    ../src/pressurechart.cpp \
    ../src/settings.cpp \
    ../src/theme.cpp \
    ../src/trackpad.cpp \
    ../src/translator.cpp

# The real catalogs, so a test can tell that they load.
RESOURCES += ../src/resources.qrc
