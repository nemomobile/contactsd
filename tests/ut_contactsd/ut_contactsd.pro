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

CONFIG += test

QT -= gui
QT += testlib
QT += dbus

CONFIG(coverage):{
QMAKE_CXXFLAGS +=  -ftest-coverage -fprofile-arcs
LIBS += -lgcov
}

include(../common/test-common.pri)

TARGET = ut_contactsd
target.path = $$BINDIR

INCLUDEPATH += $$TOP_SOURCEDIR/src

HEADERS += test-contactsd.h \
    $$TOP_SOURCEDIR/src/contactsimportprogressadaptor.h \
    $$TOP_SOURCEDIR/src/contactsdpluginloader.h \
    $$TOP_SOURCEDIR/src/importstate.h \
    $$TOP_SOURCEDIR/src/debug.h \
    $$TOP_SOURCEDIR/src/base-plugin.h

SOURCES += test-contactsd.cpp \
    $$TOP_SOURCEDIR/src/contactsimportprogressadaptor.cpp  \
    $$TOP_SOURCEDIR/src/contactsdpluginloader.cpp \
    $$TOP_SOURCEDIR/src/importstate.cpp \
    $$TOP_SOURCEDIR/src/debug.cpp \
    $$TOP_SOURCEDIR/src/base-plugin.cpp

DEFINES += CONTACTSD_PLUGINS_DIR=\\\"$$LIBDIR/$${PACKAGENAME}-1.0/plugins\\\"

#gcov stuff
CONFIG(coverage):{
QMAKE_CLEAN += *.gcov $(OBJECTS_DIR)*.gcda $(OBJECTS_DIR)*.gcno gcov.analysis gcov.analysis.summary
gcov.target = gcov
cov.CONFIG = recursive
gcov.commands = for d in $$SOURCES; do (gcov -b -a -u -o $(OBJECTS_DIR) \$$$$d >> gcov.analysis ); done;
gcov.depends = $(TARGET)
QMAKE_EXTRA_TARGETS += gcov
}

INSTALLS += target
