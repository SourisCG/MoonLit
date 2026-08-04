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

/* Single source of truth for the MoonLit look: a Dracula-inspired dark
 * palette with the MoonLit purple as the brand accent. All widgets build
 * their stylesheets from these constants so the theme stays consistent. */
namespace MoonLitTheme {

inline QColor bgDeep() { return QColor(0x1e, 0x1f, 0x29); } /* window / dashboard */
inline QColor bgSurface() { return QColor(0x28, 0x2a, 0x36); } /* cards, panels, inputs */
inline QColor bgElevated() { return QColor(0x34, 0x37, 0x46); } /* hover / selection */
inline QColor border() { return QColor(0x44, 0x47, 0x5a); } /* Dracula current-line */
inline QColor text() { return QColor(0xf8, 0xf8, 0xf2); } /* Dracula foreground */
inline QColor textMuted() { return QColor(0x62, 0x72, 0xa4); } /* Dracula comment */
inline QColor accent() { return QColor(0x76, 0x67, 0xf5); } /* MoonLit purple */
inline QColor accentHover() { return QColor(0x8b, 0x7c, 0xf9); }
inline QColor rec() { return QColor(0xff, 0x55, 0x55); } /* Dracula red */
inline QColor ok() { return QColor(0x50, 0xfa, 0x7b); } /* Dracula green */
inline QColor warning() { return QColor(0xff, 0xb8, 0x6c); } /* Dracula orange */

inline QString css(const QColor &color)
{
	return color.name();
}

} // namespace MoonLitTheme
