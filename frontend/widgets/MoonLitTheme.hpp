/******************************************************************************
    MoonLit theme (Dracula x MoonLit)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <QColor>
#include <QString>

/* Single source of truth for the MoonLit look: a night-sky palette built on
 * the five brand colors (asphalt #080303, aubergine #37060d, big stone
 * #13232f, night rider #110808, ebony #111627) with warm white text and a
 * few derived interaction tones. All widgets build their stylesheets from
 * these constants so the theme stays consistent. */
namespace MoonLitTheme {

inline QColor bgDeep() { return QColor(0x08, 0x03, 0x03); } /* asphalt: window / sky behind everything */
inline QColor bgSurface() { return QColor(0x11, 0x16, 0x27); } /* ebony: cards, panels, inputs */
inline QColor bgElevated() { return QColor(0x13, 0x23, 0x2f); } /* big stone: hover / selection */
inline QColor border() { return QColor(0x37, 0x06, 0x0d); } /* aubergine: borders */
inline QColor accent() { return QColor(0x37, 0x06, 0x0d); } /* aubergine: brand accent */
inline QColor accentHover() { return QColor(0x57, 0x11, 0x1f); } /* lifted aubergine: interactive hover */
inline QColor night() { return QColor(0x11, 0x08, 0x08); } /* night rider: pressed / deep panels */
inline QColor text() { return QColor(0xf2, 0xe9, 0xe9); } /* warm white */
inline QColor textMuted() { return QColor(0x8b, 0x7d, 0x80); } /* warm grey */
inline QColor star() { return QColor(0xff, 0xf2, 0xea); } /* starfield white */
inline QColor rec() { return QColor(0xe5, 0x48, 0x4d); } /* recording red */
inline QColor ok() { return QColor(0x6e, 0xe7, 0xa0); } /* saved green */
inline QColor warning() { return QColor(0xf5, 0xb8, 0x6c); } /* warning orange */

inline QString css(const QColor &color)
{
	return color.name();
}

} // namespace MoonLitTheme
