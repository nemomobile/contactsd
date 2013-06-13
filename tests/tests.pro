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

include(common/packagename.pri)

TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += libtelepathy ut_birthdayplugin ut_telepathyplugin

UNIT_TESTS += ut_birthdayplugin ut_telepathyplugin

testxml.target = tests.xml
testxml.commands = sh $$PWD/mktests.sh $$UNIT_TESTS >$@ || rm -f $@
testxml.depends = $$UNIT_TESTS

install_testxml.files = $$testxml.target
install_testxml.path = /opt/tests/$${PACKAGENAME}/test-definition/
install_testxml.depends = $$testxml.target
install_testxml.CONFIG = no_check_exist

install_extrascripts.files = with-session-bus.sh session.conf
install_extrascripts.path = /opt/tests/$${PACKAGENAME}/
install_extrascripts.depends = $$UNIT_TESTS
install_extrascripts.CONFIG = no_check_exist

INSTALLS += install_testxml install_extrascripts

QMAKE_EXTRA_TARGETS += testxml
QMAKE_DISTCLEAN += $$testxml.target

POST_TARGETDEPS += $$testxml.target
