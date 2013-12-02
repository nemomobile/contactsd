/** This file is part of Contacts daemon
 **
 ** Copyright (c) 2010-2011 Nokia Corporation and/or its subsidiary(-ies).
 ** Contact:  Nokia Corporation (info@qt.nokia.com)
 **
 ** GNU Lesser General Public License Usage
 ** This file may be used under the terms of the GNU Lesser General Public License
 ** version 2.1 as published by the Free Software Foundation and appearing in the
 ** file LICENSE.LGPL included in the packaging of this file.  Please review the
 ** following information to ensure the GNU Lesser General Public License version
 ** 2.1 requirements will be met:
 ** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 **
 ** In addition, as a special exception, Nokia gives you certain additional rights.
 ** These rights are described in the Nokia Qt LGPL Exception version 1.1, included
 ** in the file LGPL_EXCEPTION.txt in this package.
 **
 ** Other Usage
 ** Alternatively, this file may be used in accordance with the terms and
 ** conditions contained in a signed written agreement between you and Nokia.
 **/

#include "util.h"
#include "debug.h"

#include <QStringList>
#include <QChar>

template<typename F1, typename F2>
void updateNameDetail(F1 getter, F2 setter, QContactName *nameDetail, const QString &value)
{
    QString existing((nameDetail->*getter)());
    if (!existing.isEmpty()) {
        existing.append(QChar::fromLatin1(' '));
    }
    (nameDetail->*setter)(existing + value);
}

void Contactsd::Util::decomposeNameDetails(const QString &formattedName, QContactName *nameDetail)
{
    // Try to parse the structure from the formatted name
    // TODO: Use MBreakIterator for localized splitting
    QStringList tokens(formattedName.split(QChar::fromLatin1(' '), QString::SkipEmptyParts));
    if (tokens.count() >= 2) {
        QString format;
        if (tokens.count() == 2) {
            //: Format string for allocating 2 tokens to name parts - 2 characters from the set [FMLPS]
            //% "FL"
            format = qtTrId("qtn_name_structure_2_tokens");
        } else if (tokens.count() == 3) {
            //: Format string for allocating 3 tokens to name parts - 3 characters from the set [FMLPS]
            //% "FML"
            format = qtTrId("qtn_name_structure_3_tokens");
        } else if (tokens.count() > 3) {
            //: Format string for allocating 4 tokens to name parts - 4 characters from the set [FMLPS]
            //% "FFML"
            format = qtTrId("qtn_name_structure_4_tokens");

            // Coalesce the leading tokens together to limit the possibilities
            int excess = tokens.count() - 4;
            if (excess > 0) {
                QString first(tokens.takeFirst());
                while (--excess >= 0) {
                    // TODO: locale-specific join?
                    first += QChar::fromLatin1(' ') + tokens.takeFirst();
                }
                tokens.prepend(first);
            }
        }

        if (format.length() != tokens.length()) {
            qWarning() << "Invalid structure format for" << tokens.count() << "tokens:" << format;
        } else {
            Q_FOREACH (const QChar &part, format) {
                const QString token(tokens.takeFirst());
                switch (part.toUpper().toLatin1()) {
                    case 'F': updateNameDetail(&QContactName::firstName, &QContactName::setFirstName, nameDetail, token); break;
                    case 'M': updateNameDetail(&QContactName::middleName, &QContactName::setMiddleName, nameDetail, token); break;
                    case 'L': updateNameDetail(&QContactName::lastName, &QContactName::setLastName, nameDetail, token); break;
                    case 'P': updateNameDetail(&QContactName::prefix, &QContactName::setPrefix, nameDetail, token); break;
                    case 'S': updateNameDetail(&QContactName::suffix, &QContactName::setSuffix, nameDetail, token); break;
                    default:
                        qWarning() << "Invalid structure format character:" << part;
                }
            }
        }
    }
}