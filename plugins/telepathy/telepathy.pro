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

TEMPLATE = lib
QT -= gui
QT += dbus network

CONFIG += plugin link_pkgconfig

PKGCONFIG += Qt5Contacts
PKGCONFIG += TelepathyQt5 qtcontacts-sqlite-qt5-extensions
DEFINES *= USING_QTPIM

system(qdbusxml2cpp -c BuddyManagementAdaptor -a buddymanagementadaptor.h:buddymanagementadaptor.cpp com.nokia.contacts.buddymanagement.xml)

CONFIG(coverage):{
QMAKE_CXXFLAGS += -c -g  --coverage -ftest-coverage -fprofile-arcs
LIBS += -lgcov
}

DEFINES += QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_ASCII

TRANSLATIONS_INSTALL_PATH = "/usr/share/translations"
DEFINES += TRANSLATIONS_INSTALL_PATH=\"\\\"\"$${TRANSLATIONS_INSTALL_PATH}\"\\\"\"

INCLUDEPATH += $$TOP_SOURCEDIR/src
DEFINES += ENABLE_DEBUG

HEADERS  = cdtpaccount.h \
    cdtpaccountcache.h \
    cdtpaccountcacheloader.h \
    cdtpaccountcachewriter.h \
    types.h \
    cdtpcontact.h \
    cdtpcontroller.h \
    cdtpplugin.h \
    cdtpstorage.h \
    buddymanagementadaptor.h \
    cdtpavatarupdate.h

SOURCES  = cdtpaccount.cpp \
    cdtpaccountcacheloader.cpp \
    cdtpaccountcachewriter.cpp \
    cdtpcontact.cpp \
    cdtpcontroller.cpp \
    cdtpplugin.cpp \
    cdtpstorage.cpp \
    buddymanagementadaptor.cpp \
    cdtpavatarupdate.cpp

VERSIONED_PACKAGENAME=contactsd-1.0

TARGET = telepathyplugin
target.path = $$LIBDIR/$${VERSIONED_PACKAGENAME}/plugins

xml.files = com.nokia.contacts.buddymanagement.xml
xml.path = $$INCLUDEDIR/$${VERSIONED_PACKAGENAME}

INSTALLS += target xml

OTHER_FILES += \
    com.nokia.contacts.buddymanagement.xml
