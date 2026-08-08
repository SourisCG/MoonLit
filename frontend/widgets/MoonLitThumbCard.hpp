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

#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

/* Custom-painted clip card: the video thumbnail always fills a 16:9 box at
 * the top of the card (no fat margins), with the title (and an optional
 * detail line) below. Painted, never stylesheet-styled, so the fixed size
 * is exact (Qt style sheets rewrite a widget's min/max box model and would
 * squeeze or stretch the card). Used for the dashboard recents and the
 * library grid. */
class MoonLitThumbCard final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitThumbCard(QWidget *parent = nullptr);

	void setThumbnail(const QPixmap &pixmap);
	void setTitle(const QString &title, const QString &detail = QString());
	void setSelected(bool selected);
	bool selected() const { return selected_; }

signals:
	void clicked();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	QPixmap thumbnail_;
	QString title_;
	QString detail_;
	bool selected_ = false;
	bool hovered_ = false;
};
