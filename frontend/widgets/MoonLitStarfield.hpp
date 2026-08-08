/******************************************************************************
    MoonLit starfield

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
******************************************************************************/

#pragma once

#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

class QTimer;

/* Twinkling starfield background, a C/Qt port of the classic CSS/JS night
 * sky: ~150 stars with random size, twinkle period and delay, painted by a
 * single 30 fps timer. Star positions are stored normalized (0..1) so the
 * sky always fills the widget, no matter how the window is resized. The
 * widget paints the asphalt background plus the stars, so content placed on
 * top in a layout shows the stars through its transparent parts. */
class MoonLitStarfield final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitStarfield(QWidget *parent = nullptr, int starCount = 150);

	/* The widget fills its rect with the asphalt sky by default (used as a
	 * root surface). Set false when the starfield sits over content painted
	 * by its parent: it then draws only the stars, so its 30 fps repaints
	 * never clobber sibling widgets painted above it. */
	void setPaintBackground(bool paint);

protected:
	void paintEvent(QPaintEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;

private:
	struct Star {
		double x = 0.0; /* normalized 0..1 */
		double y = 0.0;
		double size = 1.0;  /* px */
		double period = 2.0; /* twinkle period, seconds */
		double delay = 0.0;  /* animation delay, seconds */
	};

	QVector<Star> stars_;
	QTimer *timer_ = nullptr;
	QElapsedTimer clock_;
	bool paintBackground_ = true;
};
