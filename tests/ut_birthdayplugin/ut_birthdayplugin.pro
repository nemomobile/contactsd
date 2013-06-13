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

include(../common/test-common.pri)

TARGET = ut_birthdayplugin
target.path = /opt/tests/$${PACKAGENAME}

include(check.pri)
include(tests.pri)

# Hack: mkcal adds /usr/include/meegotouch to include path, and alphabetic CONFIG
# always puts that before mlocale, resulting in link errors. Force mlocale to be
# first.
equals(QT_MAJOR_VERSION, 4): INCLUDEPATH += /usr/include/mlocale
equals(QT_MAJOR_VERSION, 5): INCLUDEPATH += /usr/include/mlocale5

CONFIG += test link_pkgconfig

QT -= gui
QT += testlib
DEFINES += ENABLE_DEBUG

equals(QT_MAJOR_VERSION, 4) {
    CONFIG += mobility mlocale mkcal
    MOBILITY = contacts
    PKGCONFIG += mlite
}
equals(QT_MAJOR_VERSION, 5) {
    PKGCONFIG += Qt5Contacts
    PKGCONFIG += mlite5 mlocale5 libmkcal-qt5 libkcalcoren-qt5
    DEFINES *= USING_QTPIM
}

CONFIG(coverage):{
QMAKE_CXXFLAGS +=  -ftest-coverage -fprofile-arcs
LIBS += -lgcov
}

HEADERS += test-birthday-plugin.h

SOURCES += test-birthday-plugin.cpp

#gcov stuff
CONFIG(coverage):{
INCLUDEPATH += $$TOP_SOURCEDIR/src
HEADERS += $$TOP_SOURCEDIR/plugins/birthday/cdbirthdaycalendar.h \
    $$TOP_SOURCEDIR/plugins/birthday/cdbirthdaycontroller.h \
    $$TOP_SOURCEDIR/plugins/birthday/cdbirthdayplugin.h

SOURCES += $$TOP_SOURCEDIRplugins/birthday/cdbirthdaycalendar.cpp \
    $$TOP_SOURCEDIR/plugins/birthday/cdbirthdaycontroller.cpp \
    $$TOP_SOURCEDIR/plugins/birthday/cdbirthdayplugin.cpp

DEFINES += CONTACTSD_PLUGINS_DIR=\\\"$$TOP_SOURCEDIR/plugins/telepathy/\\\"

QMAKE_CLEAN += *.gcov $(OBJECTS_DIR)*.gcda $(OBJECTS_DIR)*.gcno gcov.analysis gcov.analysis.summary

gcov.target = gcov
cov.CONFIG = recursive
gcov.commands = for d in $$SOURCES; do (gcov -b -a -u -o $(OBJECTS_DIR) \$$$$d >> gcov.analysis ); done;
gcov.depends = $(TARGET)
QMAKE_EXTRA_TARGETS += gcov
}

INSTALLS += target
