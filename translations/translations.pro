# CONFIG PARAMS --------------------------------------------------------------
INSTALL_PREFIX = /usr

# Name of the catalog:
CATALOGNAME = contactsd

# Paths of source code files:
SOURCEPATHS = $${_PRO_FILE_PWD_}/../src $${_PRO_FILE_PWD_}/../plugins 

# Installation paths:
QM_INSTALL_PATH = $${INSTALL_PREFIX}/share/translations/
TS_INSTALL_PATH = $${INSTALL_PREFIX}/share/translations/source/

#-----------------------------------------------------------------------------
# DO NOT EDIT THIS PART ------------------------------------------------------
#-----------------------------------------------------------------------------

TEMPLATE = aux
SUBDIRS  =
CONFIG   = warn_on

# "unarm" the primary target generation by disabling linking
QMAKE_LFLAGS = --version

# translation input/output
TS_FILENAME = $${_PRO_FILE_PWD_}/$${CATALOGNAME}.ts
QM_FILENAME = $${_PRO_FILE_PWD_}/$${CATALOGNAME}.qm

# LUPDATE and LRELEASE --------------------------------------------------------
LUPDATE_CMD = lupdate \
              $${SOURCEPATHS} \
              -ts $${TS_FILENAME} && \
              lrelease \
              -idbased \
              $${TS_FILENAME} \
              -qm $${QM_FILENAME}

# extra target for generating the .qm file
lupdate.target       = .lupdate
lupdate.commands     = $$LUPDATE_CMD
QMAKE_EXTRA_TARGETS += lupdate
PRE_TARGETDEPS      += .lupdate

# installation target for the .ts file
tsfile.CONFIG += no_check_exist no_link
tsfile.files   = $${TS_FILENAME}
tsfile.path    = $${TS_INSTALL_PATH}
tsfile.target  = tsfile
INSTALLS      += tsfile

# installation target for the .qm file
qmfile.CONFIG  += no_check_exist no_link
qmfile.files    = $${QM_FILENAME}
qmfile.path     = $${QM_INSTALL_PATH}
qmfile.target   = qmfile
INSTALLS       += qmfile

QMAKE_CLEAN += $${TS_FILENAME} $${QM_FILENAME}

# Engineering english fallback ----------------------
EE_QM_FILENAME = $${_PRO_FILE_PWD_}/$${CATALOGNAME}_eng_en.qm

engineering_english.commands += lrelease -idbased $${TS_FILENAME} -qm $${EE_QM_FILENAME}
engineering_english.CONFIG += no_check_exist no_link
engineering_english.depends = lupdate
engineering_english.input = $${TS_FILENAME}
engineering_english.output = $${EE_QM_FILENAME}

engineering_english_install.path = $${QM_INSTALL_PATH}
engineering_english_install.files = $${EE_QM_FILENAME}
engineering_english_install.CONFIG += no_check_exist

QMAKE_EXTRA_TARGETS += engineering_english

PRE_TARGETDEPS += engineering_english

INSTALLS += engineering_english_install

# End of File
