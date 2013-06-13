TEMPLATE = subdirs
CONFIG += ordered
QT -= gui

SUBDIRS += src plugins tests

equals(QT_MAJOR_VERSION, 4): PACKAGENAME=contactsd
equals(QT_MAJOR_VERSION, 5): PACKAGENAME=contactsd-qt5

PKGCONFIG_FILE=$${PACKAGENAME}-1.0.pc

pkgconfig.path=$$LIBDIR/pkgconfig
pkgconfig.files=$${PKGCONFIG_FILE}

backupconf.path=$$PREFIX/share/backup-framework/applications/
backupconf.files=$${PACKAGENAME}.conf

INSTALLS += pkgconfig backupconf

check.target = check
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check

confclean.depends += distclean
confclean.commands += \
    $(DEL_FILE) $$TOP_BUILDDIR/.qmake.cache \
    $(DEL_FILE) $$TOP_BUILDDIR/$${PKGCONFIG_FILE}
QMAKE_EXTRA_TARGETS += confclean

OTHER_FILES += configure

# Run configure script when building the project from tools like QtCreator
isEmpty(CONFIGURED):system('$$PWD/configure')
