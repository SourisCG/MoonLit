/******************************************************************************
    MoonLit navigation bar

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

#include <QWidget>

class QButtonGroup;
class QPushButton;

/* Medal-style left navigation rail: MoonLit wordmark on top, Inicio and
 * Clips for the main views, Ajustes pinned to the bottom. The active item
 * shows as a big-stone pill with an aubergine left edge. */
class MoonLitNavBar final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitNavBar(QWidget *parent = nullptr);

	enum class Item { Home, Library, Settings };

	void setActiveItem(Item item);

signals:
	void homeRequested();
	void libraryRequested();
	void settingsRequested();

private:
	QButtonGroup *group_ = nullptr;
};
