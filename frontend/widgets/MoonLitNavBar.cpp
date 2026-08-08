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

#include "MoonLitNavBar.hpp"

#include "MoonLitTheme.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kNavWidth = 200;
} // namespace

MoonLitNavBar::MoonLitNavBar(QWidget *parent) : QWidget(parent)
{
	using namespace MoonLitTheme;
	setObjectName(QStringLiteral("moonlitNav"));
	setFixedWidth(kNavWidth);

	/* Slightly translucent so the starfield glows through the rail. */
	setStyleSheet(QStringLiteral(R"(
        #moonlitNav {
            background: rgba(%1, %2, %3, 200);
            border-right: 1px solid %4;
        }
        QLabel#moonlitNavLogo {
            color: %5;
            font-size: 19px;
            font-weight: 700;
            letter-spacing: 1px;
        }
        QPushButton#moonlitNavItem {
            text-align: left;
            padding: 12px 14px;
            margin: 2px 10px;
            border: 0;
            border-left: 3px solid transparent;
            border-radius: 8px;
            background: transparent;
            color: %6;
            font-size: 14px;
            font-weight: 500;
        }
        QPushButton#moonlitNavItem:hover {
            background: %7;
            color: %5;
        }
        QPushButton#moonlitNavItem:checked {
            background: %8;
            color: %5;
            border-left: 3px solid %9;
        }
        QPushButton#moonlitNavItem:pressed {
            background: %10;
        }
    )")
			.arg(bgDeep().red())
			.arg(bgDeep().green())
			.arg(bgDeep().blue())
			.arg(css(border()), css(text()), css(textMuted()), css(bgElevated()), css(bgSurface()),
			     css(accent()), css(night())));

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 10, 0, 10);
	layout->setSpacing(2);

	auto *logoRow = new QHBoxLayout();
	logoRow->setContentsMargins(16, 0, 0, 0);
	logoRow->setSpacing(10);
	auto *icon = new QLabel(this);
	icon->setPixmap(QIcon(QStringLiteral(":/res/images/moonlit-icon.png")).pixmap(30, 30));
	auto *logo = new QLabel(QStringLiteral("MoonLit"), this);
	logo->setObjectName(QStringLiteral("moonlitNavLogo"));
	logoRow->addWidget(icon);
	logoRow->addWidget(logo);
	logoRow->addStretch(1);
	layout->addLayout(logoRow);
	layout->addSpacing(12);

	group_ = new QButtonGroup(this);
	group_->setExclusive(true);

	auto addItem = [this, layout](const QString &text, Item item) {
		auto *button = new QPushButton(text, this);
		button->setObjectName(QStringLiteral("moonlitNavItem"));
		button->setCheckable(true);
		button->setCursor(Qt::PointingHandCursor);
		group_->addButton(button, static_cast<int>(item));
		layout->addWidget(button);
		return button;
	};

	QPushButton *home = addItem(QStringLiteral("Inicio"), Item::Home);
	QPushButton *library = addItem(QStringLiteral("Clips"), Item::Library);
	connect(home, &QPushButton::clicked, this, &MoonLitNavBar::homeRequested);
	connect(library, &QPushButton::clicked, this, &MoonLitNavBar::libraryRequested);

	layout->addStretch(1);

	QPushButton *settings = addItem(QStringLiteral("Ajustes"), Item::Settings);
	connect(settings, &QPushButton::clicked, this, &MoonLitNavBar::settingsRequested);

	home->setChecked(true);
}

void MoonLitNavBar::setActiveItem(Item item)
{
	if (QAbstractButton *button = group_->button(static_cast<int>(item))) {
		button->setChecked(true);
	}
}
