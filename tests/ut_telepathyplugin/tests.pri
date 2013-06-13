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

daemon.target = with-daemon.sh
daemon.depends = $$PWD/with-daemon.sh.in
daemon.path = /opt/tests/$${PACKAGENAME}
daemon.commands = \
    sed -e \"s,@BINDIR@,$$BINDIR,g\" \
        -e \"s,@PLUGINDIR@,$$LIBDIR/$${PACKAGENAME}-1.0/plugins,g\" \
    $< > $@ && chmod +x $@ || rm -f $@

wrapper.target = ut_telepathyplugin-wrapper.sh
wrapper.depends = $$PWD/ut_telepathyplugin-wrapper.sh.in
wrapper.path = /opt/tests/$${PACKAGENAME}/ut_telepathyplugin
wrapper.commands = \
    sed -e \"s,@SCRIPTDIR@,/opt/tests/$${PACKAGENAME},g\" \
        -e \"s,@BINDIR@,$$BINDIR,g\" \
        -e \"s,@WITH_DAEMON@,$$daemon.target,g\" \
    $< > $@ && chmod +x $@ || rm -f $@

install_extrascripts.files = $$wrapper.target $$daemon.target
install_extrascripts.path = /opt/tests/$${PACKAGENAME}/ut_telepathyplugin
install_extrascripts.depends = daemon wrapper
install_extrascripts.CONFIG = no_check_exist

QMAKE_INSTALL_FILE = cp -p
QMAKE_EXTRA_TARGETS += daemon wrapper
QMAKE_CLEAN += $$daemon.target $$wrapper.target

PRE_TARGETDEPS += $$daemon.target $$wrapper.target
INSTALLS += install_extrascripts

