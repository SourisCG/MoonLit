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

#include "MoonLitStarfield.hpp"

#include "MoonLitTheme.hpp"

#include <QHideEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QShowEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
/* Star twinkle cycle bounds, matching the CSS/JS reference sky. */
constexpr double kMinPeriod = 1.5;
constexpr double kMaxPeriod = 3.5;
constexpr double kMaxDelay = 3.0;
constexpr double kMinSize = 0.6;
constexpr double kMaxSize = 2.6;
constexpr int kFrameMs = 33; /* ~30 fps */
} // namespace

MoonLitStarfield::MoonLitStarfield(QWidget *parent, int starCount) : QWidget(parent)
{
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_NoSystemBackground);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	QRandomGenerator *rng = QRandomGenerator::global();
	stars_.reserve(starCount);
	for (int index = 0; index < starCount; ++index) {
		Star star;
		star.x = rng->generateDouble();
		star.y = rng->generateDouble();
		star.size = kMinSize + rng->generateDouble() * (kMaxSize - kMinSize);
		star.period = kMinPeriod + rng->generateDouble() * (kMaxPeriod - kMinPeriod);
		star.delay = rng->generateDouble() * kMaxDelay;
		stars_.append(star);
	}

	timer_ = new QTimer(this);
	timer_->setInterval(kFrameMs);
	connect(timer_, &QTimer::timeout, this, [this]() {
		if (paintBackground_) {
			update();
			return;
		}
		/* Overlay mode: only the tiny star rects are marked dirty, so the
		 * repaint can never wipe out sibling widgets painted above. */
		for (const Star &star : stars_) {
			const int sx = static_cast<int>(star.x * width());
			const int sy = static_cast<int>(star.y * height());
			update(sx - 1, sy - 1, 4, 4);
		}
	});
	clock_.start();
	timer_->start();
}

void MoonLitStarfield::setPaintBackground(bool paint)
{
	paintBackground_ = paint;
	/* Opaque only when the widget paints the full sky itself; otherwise the
	 * parent must paint behind it (Qt skips the parent where an opaque child
	 * lies, leaving garbage in the star-only areas). */
	setAttribute(Qt::WA_OpaquePaintEvent, paint);
	setAttribute(Qt::WA_TranslucentBackground, !paint);
	update();
}

void MoonLitStarfield::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	if (paintBackground_) {
		painter.fillRect(rect(), MoonLitTheme::bgDeep());
	}
	painter.setPen(Qt::NoPen);
	painter.setRenderHint(QPainter::Antialiasing, false);

	const double now = clock_.elapsed() / 1000.0;
	const int w = width();
	const int h = height();

	QColor color = MoonLitTheme::star();
	for (const Star &star : stars_) {
		/* Triangular wave over the star's phase gives the twinkle in and out;
		 * squared makes the dip quicker so stars spend most time bright. */
		const double phase =
			std::fmod(std::max(0.0, now - star.delay), star.period) / star.period;
		const double wave = 1.0 - std::fabs(2.0 * phase - 1.0);
		color.setAlpha(static_cast<int>(45.0 + 200.0 * wave * wave));

		const int sx = static_cast<int>(star.x * w);
		const int sy = static_cast<int>(star.y * h);
		const int size = static_cast<int>(star.size + 0.5);
		painter.setBrush(color);
		if (size <= 1) {
			painter.drawRect(sx, sy, 1, 1);
		} else {
			painter.drawRect(sx, sy, size, size);
		}
	}
}

void MoonLitStarfield::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	timer_->start();
}

void MoonLitStarfield::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	timer_->stop();
}
