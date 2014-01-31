include(../common/test-common.pri)

TARGET = ut_simplugin
target.path = /opt/tests/$${PACKAGENAME}/$$TARGET

CONFIG += test link_pkgconfig

QT -= gui
QT += dbus testlib
DEFINES += ENABLE_DEBUG

PKGCONFIG += mlite5 Qt5Contacts Qt5Versit qofono-qt5
DEFINES *= USING_QTPIM

INCLUDEPATH += \
    ../../plugins/sim \
    ../../src

HEADERS += \
    test-sim-plugin.h \
    ../../plugins/sim/cdsimcontroller.h \
    ../../src/util.h

SOURCES += \
    test-sim-plugin.cpp \
    ../../plugins/sim/cdsimcontroller.cpp \
    ../../src/util.cpp

INSTALLS += target
