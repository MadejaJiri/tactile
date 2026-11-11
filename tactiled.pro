QT = core network

CONFIG += c++17 release cmdline
TARGET = tactiled
TEMPLATE = app

HEADERS += \
    src/daemon.h \
    src/driver.h \
    src/settings.h \
    src/trackpad.h \
    src/version.h

SOURCES += \
    src/daemon_main.cpp \
    src/daemon.cpp \
    src/driver.cpp \
    src/settings.cpp \
    src/trackpad.cpp
