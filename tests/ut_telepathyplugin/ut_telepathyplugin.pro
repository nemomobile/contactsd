# This file is part of Contacts daemon
#
# Copyright (c) 2010-2011 Nokia Corporation and/or its subsidiary(-ies).
#
# Contact:  Nokia Corporation (info@qt.nokia.com)
#
# GNU Lesser General Public License Usage
# This file may be used under the terms of the GNU Lesser General Public License
# version 2.1 as published by the Free Software Foundation and appearing in the
# file LICENSE.LGPL included in the packaging of this file.  Please review the
# following information to ensure the GNU Lesser General Public License version
# 2.1 requirements will be met:
# http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
#
# In addition, as a special exception, Nokia gives you certain additional rights.
# These rights are described in the Nokia Qt LGPL Exception version 1.1, included
# in the file LGPL_EXCEPTION.txt in this package.
#
# Other Usage
# Alternatively, this file may be used in accordance with the terms and
# conditions contained in a signed written agreement between you and Nokia.

PRE_TARGETDEPS += ../libtelepathy/libtelepathy.a

TARGET = ut_telepathyplugin
target.path = /opt/tests/contactsd

include(check.pri)
include(tests.pri)
include(../common/test-common.pri)

TEMPLATE = app

CONFIG += test qt mobility
QT += testlib dbus
QT -= gui
MOBILITY += contacts
CONFIG += link_pkgconfig
PKGCONFIG += TelepathyQt4 telepathy-glib
DEFINES += QT_NO_KEYWORDS
DEFINES += ENABLE_DEBUG

system(cp $$PWD/../../plugins/telepathy/com.nokia.contacts.buddymanagement.xml .)
system(qdbusxml2cpp -c BuddyManagementInterface -p buddymanagementinterface.h:buddymanagementinterface.cpp com.nokia.contacts.buddymanagement.xml)

INCLUDEPATH += ..
QMAKE_LIBDIR += ../libtelepathy
LIBS += -ltelepathy

CONFIG(coverage): {
QMAKE_CXXFLAGS += -c -g --coverage -ftest-coverage -fprofile-arcs
LIBS += -lgcov
}

HEADERS += debug.h \
    test-telepathy-plugin.h \
    test-expectation.h \
    test.h \
    buddymanagementinterface.h

SOURCES += debug.cpp \
    test-telepathy-plugin.cpp \
    test-expectation.cpp \
    test.cpp \
    buddymanagementinterface.cpp

#for gcov stuff
CONFIG(coverage): {
INCLUDEPATH += $$TOP_SOURCEDIR/src
HEADERS += $$TOP_SOURCEDIR/plugins/telepathy/cdtpaccount.h \
    $$TOP_SOURCEDIR/plugins/telepathy/cdtpcontact.h \
    $$TOP_SOURCEDIR/plugins/telepathy/cdtpcontroller.h \
    $$TOP_SOURCEDIR/plugins/telepathy/cdtpplugin.h \
    $$TOP_SOURCEDIR/plugins/telepathy/cdtpstorage.h \
    $$TOP_SOURCEDIR/plugins/telepathy/buddymanagementadaptor.h \
    $$TOP_SOURCEDIR/plugins/telepathy/redliststorage.h


SOURCES += $$TOP_SOURCEDIR/plugins/telepathy/cdtpaccount.cpp \
    $$TOP_SOURCEDIR/plugins/telepathy/cdtpcontact.cpp \
    $$TOP_SOURCEDIR/plugins/telepathy/cdtpcontroller.cpp \
    $$TOP_SOURCEDIR/plugins/telepathy/cdtpplugin.cpp \
    $$TOP_SOURCEDIR/plugins/telepathy/cdtpstorage.cpp \
    $$TOP_SOURCEDIR/plugins/telepathy/redliststorage.cpp \
    $$TOP_SOURCEDIR/plugins/telepathy/buddymanagementadaptor.cpp \
    $$TOP_SOURCEDIR/plugins/telepathy/redliststorage.cpp

#to use start the daemon from code

HEADERS += $$TOP_SOURCEDIR/src/contactsd.h \
    $$TOP_SOURCEDIR/src/contactsimportprogressadaptor.h \
    $$TOP_SOURCEDIR/src/contactsdpluginloader.h \
    $$TOP_SOURCEDIR/src/importstate.h

SOURCES += $$TOP_SOURCEDIR/src/contactsd.cpp \
    $$TOP_SOURCEDIR/src/contactsimportprogressadaptor.cpp  \
    $$TOP_SOURCEDIR/src/contactsdpluginloader.cpp \
    $$TOP_SOURCEDIR/src/importstate.cpp

DEFINES += CONTACTSD_PLUGINS_DIR=\\\"$$TOP_SOURCEDIR/plugins/telepathy/\\\"

QMAKE_CLEAN += *.gcov $(OBJECTS_DIR)*.gcda $(OBJECTS_DIR)*.gcno gcov.analysis gcov.analysis.summary

gcov.target = gcov
gcov.CONFIG = recursive
gcov.commands += for d in $$SOURCES; do (gcov -a -c -o $(OBJECTS_DIR) \$$$$d >> gcov.analysis ); done;
gcov.depends = $(TARGET)

QMAKE_EXTRA_TARGETS += gcov
}

INSTALLS += target
