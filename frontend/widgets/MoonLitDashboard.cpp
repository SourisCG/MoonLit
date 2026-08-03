/******************************************************************************
    MoonLit dashboard (Medal-style)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
******************************************************************************/

#include "MoonLitDashboard.hpp"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QLabel *makeLabel(const QString &text, QWidget *parent = nullptr)
{
	auto *label = new QLabel(text, parent);
	label->setWordWrap(true);
	return label;
}

QPushButton *makeCardButton(QWidget *parent)
{
	auto *button = new QPushButton(parent);
	button->setFixedSize(150, 92);
	button->setIconSize(QSize(142, 58));
	button->setStyleSheet(QStringLiteral(
		"QPushButton { background: #1b1e25; border: 1px solid #2b303b; border-radius: 8px; }"
		"QPushButton:hover { border-color: #7667f5; }"));
	return button;
}

} // namespace

MoonLitDashboard::MoonLitDashboard(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("moonlitDashboard"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	setStyleSheet(QStringLiteral(R"(
        #moonlitDashboard {
            background: #0f1014;
            color: #f2f4f8;
        }
        QLabel#moonlitTitle {
            color: #ffffff;
            font-size: 26px;
            font-weight: 700;
        }
        QLabel#moonlitSubtitle, QLabel#moonlitHint, QLabel#moonlitDetail,
        QLabel#moonlitSection {
            color: #9ba3b4;
        }
        QLabel#moonlitSection {
            font-size: 13px;
            font-weight: 600;
        }
        QLabel#moonlitState {
            color: #83d89b;
            font-size: 16px;
            font-weight: 600;
        }
        QPushButton {
            min-height: 38px;
            padding: 0 16px;
            border: 1px solid #2b303b;
            border-radius: 8px;
            background: #1b1e25;
            color: #f2f4f8;
        }
        QPushButton:hover {
            border-color: #7667f5;
        }
        QPushButton:disabled {
            color: #697180;
            background: #16181e;
        }
        QPushButton#moonlitPrimary {
            border: 0;
            background: #7667f5;
            font-weight: 600;
        }
        QPushButton#moonlitPrimary:hover {
            background: #887bf7;
        }
        QPushButton#moonlitRecord {
            min-width: 0;
            min-height: 0;
            width: 120px;
            height: 120px;
            border-radius: 60px;
            border: 3px solid #e5484d;
            background: #1b1e25;
            font-size: 20px;
            font-weight: 700;
            color: #e5484d;
        }
        QPushButton#moonlitRecord:hover {
            background: #23262f;
        }
        QPushButton#moonlitRecord[active="true"] {
            background: #e5484d;
            color: #ffffff;
        }
    )"));

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(32, 24, 32, 24);
	root->setSpacing(16);

	auto *header = new QHBoxLayout();
	header->setSpacing(10);
	auto *logo = new QLabel(this);
	logo->setPixmap(QIcon(QStringLiteral(":/res/images/moonlit-icon.png")).pixmap(34, 34));
	auto *title = makeLabel(QStringLiteral("MoonLit"), this);
	title->setObjectName(QStringLiteral("moonlitTitle"));
	auto *settingsButton = new QPushButton(QStringLiteral("Ajustes"), this);
	header->addWidget(logo);
	header->addWidget(title);
	header->addStretch(1);
	header->addWidget(settingsButton);
	root->addLayout(header);

	/* Center: the big record button and the current state. */
	auto *center = new QVBoxLayout();
	center->setSpacing(10);
	center->setAlignment(Qt::AlignHCenter);
	recordButton = new QPushButton(QStringLiteral("REC"), this);
	recordButton->setObjectName(QStringLiteral("moonlitRecord"));
	recordButton->setCursor(Qt::PointingHandCursor);
	center->addWidget(recordButton, 0, Qt::AlignHCenter);
	stateLabel = makeLabel(QStringLiteral("Buffer detenido"), this);
	stateLabel->setObjectName(QStringLiteral("moonlitState"));
	stateLabel->setAlignment(Qt::AlignCenter);
	center->addWidget(stateLabel);
	gameLabel = makeLabel(QStringLiteral("Sin juego detectado"), this);
	gameLabel->setObjectName(QStringLiteral("moonlitDetail"));
	gameLabel->setAlignment(Qt::AlignCenter);
	center->addWidget(gameLabel);
	root->addLayout(center);

	/* Actions: save clip (primary) and open the library. */
	auto *actions = new QHBoxLayout();
	actions->setSpacing(10);
	saveButton = new QPushButton(QStringLiteral("Guardar clip"), this);
	saveButton->setObjectName(QStringLiteral("moonlitPrimary"));
	saveButton->setEnabled(false);
	auto *libraryButton = new QPushButton(QStringLiteral("Biblioteca"), this);
	actions->addStretch(1);
	actions->addWidget(saveButton, 2);
	actions->addWidget(libraryButton, 1);
	actions->addStretch(1);
	root->addLayout(actions);

	/* Recent clips, Medal-style. */
	auto *recentTitle = makeLabel(QStringLiteral("Recientes"), this);
	recentTitle->setObjectName(QStringLiteral("moonlitSection"));
	root->addWidget(recentTitle);
	recentGrid = new QGridLayout();
	recentGrid->setSpacing(8);
	recentGrid->setAlignment(Qt::AlignLeft);
	root->addLayout(recentGrid);
	root->addStretch(1);

	clipNoticeLabel = makeLabel(QString(), this);
	clipNoticeLabel->setObjectName(QStringLiteral("moonlitNotice"));
	clipNoticeLabel->setAlignment(Qt::AlignCenter);
	root->addWidget(clipNoticeLabel);

	auto *details = new QHBoxLayout();
	captureLabel = makeLabel(QStringLiteral("Captura: esperando configuracion"), this);
	captureLabel->setObjectName(QStringLiteral("moonlitDetail"));
	encoderLabel = makeLabel(QStringLiteral("Encoder: se enumerara desde OBS"), this);
	encoderLabel->setObjectName(QStringLiteral("moonlitDetail"));
	details->addWidget(captureLabel);
	details->addStretch(1);
	details->addWidget(encoderLabel);
	root->addLayout(details);

	connect(recordButton, &QPushButton::clicked, this, &MoonLitDashboard::replayActionRequested);
	connect(saveButton, &QPushButton::clicked, this, &MoonLitDashboard::saveClipRequested);
	connect(libraryButton, &QPushButton::clicked, this, &MoonLitDashboard::libraryRequested);
	connect(settingsButton, &QPushButton::clicked, this, &MoonLitDashboard::settingsRequested);

	noticeTimer = new QTimer(this);
	noticeTimer->setSingleShot(true);
	noticeTimer->setInterval(8000);
	connect(noticeTimer, &QTimer::timeout, this, [this]() { clipNoticeLabel->clear(); });
}

void MoonLitDashboard::setReplayState(bool active, bool stopping)
{
	if (stopping) {
		stateLabel->setText(QStringLiteral("Deteniendo buffer..."));
		recordButton->setEnabled(false);
		return;
	}

	recordButton->setEnabled(true);
	recordButton->setProperty("active", active);
	recordButton->setText(active ? QStringLiteral("■") : QStringLiteral("REC"));
	recordButton->style()->unpolish(recordButton);
	recordButton->style()->polish(recordButton);
	stateLabel->setText(active ? QStringLiteral("Buffer activo") : QStringLiteral("Buffer detenido"));
	saveButton->setEnabled(active);
}

void MoonLitDashboard::setDetectedGame(const QString &name)
{
	gameLabel->setText(name.isEmpty() ? QStringLiteral("Sin juego detectado")
						 : QStringLiteral("Juego: %1").arg(name));
}

void MoonLitDashboard::setCaptureStatus(const QString &status)
{
	captureLabel->setText(QStringLiteral("Captura: %1").arg(status));
}

void MoonLitDashboard::setEncoderStatus(const QString &status)
{
	encoderLabel->setText(QStringLiteral("Encoder: %1").arg(status));
}

void MoonLitDashboard::setClipSaved(const QString &path)
{
	clipNoticeLabel->setStyleSheet(QStringLiteral("color: #83d89b; font-weight: 600;"));
	clipNoticeLabel->setText(QStringLiteral("Clip guardado: %1").arg(QFileInfo(path).fileName()));
	noticeTimer->start();
}

void MoonLitDashboard::setClipError(const QString &message)
{
	clipNoticeLabel->setStyleSheet(QStringLiteral("color: #e98b8b; font-weight: 600;"));
	clipNoticeLabel->setText(QStringLiteral("Error: %1").arg(message));
	noticeTimer->start();
}

void MoonLitDashboard::setRecentClips(const QVector<MoonLit::Clip> &clips)
{
	recentClips_ = clips;
	rebuildRecentClips();
}

void MoonLitDashboard::rebuildRecentClips()
{
	while (QLayoutItem *item = recentGrid->takeAt(0)) {
		delete item->widget();
		delete item;
	}

	const int count = std::min<int>(9, recentClips_.size());
	for (int index = 0; index < count; ++index) {
		const MoonLit::Clip &clip = recentClips_[index];
		auto *card = makeCardButton(this);
		if (QFileInfo::exists(clip.thumbnailPath)) {
			card->setIcon(QIcon(clip.thumbnailPath));
		} else {
			card->setIcon(QIcon(QStringLiteral(":/res/images/moonlit-icon.png")));
		}
		card->setToolTip(clip.title);
		connect(card, &QPushButton::clicked, this,
			[this, id = clip.id]() { emit recentClipRequested(id); });
		recentGrid->addWidget(card, index / 3, index % 3);
	}

	recentGrid->setColumnStretch(3, 1);
}
