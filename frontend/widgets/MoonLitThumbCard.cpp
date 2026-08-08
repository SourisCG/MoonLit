/******************************************************************************
    MoonLit thumbnail card

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
******************************************************************************/

#include "MoonLitThumbCard.hpp"

#include "MoonLitTheme.hpp"

#include <QMouseEvent>
#include <QPainter>

MoonLitThumbCard::MoonLitThumbCard(QWidget *parent) : QWidget(parent)
{
	setCursor(Qt::PointingHandCursor);
}

void MoonLitThumbCard::setThumbnail(const QPixmap &pixmap)
{
	thumbnail_ = pixmap;
	update();
}

void MoonLitThumbCard::setTitle(const QString &title, const QString &detail)
{
	title_ = title;
	detail_ = detail;
	update();
}

void MoonLitThumbCard::setSelected(bool selected)
{
	selected_ = selected;
	update();
}

void MoonLitThumbCard::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	/* Card frame. */
	const QColor frame = selected_ ? MoonLitTheme::accentHover()
				       : (hovered_ ? MoonLitTheme::accent() : MoonLitTheme::border());
	painter.setPen(QPen(frame, selected_ ? 2 : 1));
	painter.setBrush(MoonLitTheme::bgSurface());
	painter.drawRoundedRect(QRect(0, 0, width() - 1, height() - 1), 8, 8);

	/* 16:9 thumbnail box: the video frames always fill it exactly, so the
	 * card never looks fatter than the thumbnail. */
	const int margin = 4;
	const int thumbWidth = width() - 2 * margin;
	const int thumbHeight = thumbWidth * 9 / 16;
	const QRect thumbRect(margin, margin, thumbWidth, thumbHeight);
	if (!thumbnail_.isNull()) {
		painter.save();
		painter.setClipRect(thumbRect.adjusted(1, 1, -1, -1));
		painter.drawPixmap(thumbRect, thumbnail_);
		painter.restore();
	} else {
		painter.fillRect(thumbRect.adjusted(1, 1, -1, -1), QColor(0, 0, 0));
	}

	/* Title (+ detail) below the thumbnail. */
	const QRect textRect(margin + 2, thumbRect.bottom() + 2, thumbWidth - 4,
			     height() - thumbRect.bottom() - 2 - margin);
	painter.setPen(MoonLitTheme::text());
	QFont titleFont = painter.font();
	titleFont.setPixelSize(11);
	painter.setFont(titleFont);
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
			 fontMetrics().elidedText(title_, Qt::ElideRight, textRect.width()));

	if (!detail_.isEmpty()) {
		QFont detailFont = titleFont;
		detailFont.setPixelSize(9);
		painter.setFont(detailFont);
		painter.setPen(MoonLitTheme::textMuted());
		painter.drawText(textRect.adjusted(0, 13, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
				 fontMetrics().elidedText(detail_, Qt::ElideRight, textRect.width()));
	}
}

void MoonLitThumbCard::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
		emit clicked();
	}
}

void MoonLitThumbCard::enterEvent(QEnterEvent *event)
{
	QWidget::enterEvent(event);
	hovered_ = true;
	update();
}

void MoonLitThumbCard::leaveEvent(QEvent *event)
{
	QWidget::leaveEvent(event);
	hovered_ = false;
	update();
}
