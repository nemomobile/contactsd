/*
 * This file is part of TelepathyQt4
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef CONTACTSD_DEBUG_H
#define CONTACTSD_DEBUG_H

#include <QDebug>

namespace Contactsd
{

Q_DECL_EXPORT void enableDebug(bool enable);
Q_DECL_EXPORT void enableWarnings(bool enable);
Q_DECL_EXPORT bool isDebugEnabled();
Q_DECL_EXPORT bool isWarningsEnabled();

class Debug
{
public:
    inline Debug() : debug(0) { }
    inline Debug(const QDebug &debug) : debug(new QDebug(debug)) { }

    inline Debug(const Debug &a) : debug(a.debug ? new QDebug(*(a.debug)) : 0) { }

    inline Debug &operator=(const Debug &a)
    {
        if (this != &a) {
            delete debug;
            debug = 0;

            if (a.debug) {
                debug = new QDebug(*(a.debug));
            }
        }

        return *this;
    }

    inline ~Debug()
    {
        delete debug;
    }

    inline Debug &space()
    {
        if (debug) {
            debug->space();
        }

        return *this;
    }

    inline Debug &nospace()
    {
        if (debug) {
            debug->nospace();
        }

        return *this;
    }

    inline Debug &maybeSpace()
    {
        if (debug) {
            debug->maybeSpace();
        }

        return *this;
    }

    template <typename T>
    inline Debug &operator<<(T a)
    {
        if (debug) {
            (*debug) << a;
        }

        return *this;
    }

private:

    QDebug *debug;
};

// The telepathy-farsight Qt 4 binding links to these - they're not API outside
// this source tarball, but they *are* ABI
Q_DECL_EXPORT Debug enabledDebug();
Q_DECL_EXPORT Debug enabledWarning();

#ifdef ENABLE_DEBUG

inline Debug debug()
{
    return enabledDebug();
}

inline Debug warning()
{
    return enabledWarning();
}

#else /* #ifdef ENABLE_DEBUG */

struct NoDebug
{
    template <typename T>
    NoDebug& operator<<(const T&)
    {
        return *this;
    }

    NoDebug& space()
    {
        return *this;
    }

    NoDebug& nospace()
    {
        return *this;
    }

    NoDebug& maybeSpace()
    {
        return *this;
    }
};

inline NoDebug debug()
{
    return NoDebug();
}

inline NoDebug warning()
{
    return NoDebug();
}

#endif /* #ifdef ENABLE_DEBUG */

} // Contactsd

#endif
