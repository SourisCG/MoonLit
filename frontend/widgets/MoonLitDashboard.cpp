/******************************************************************************
    MoonLit dashboard

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

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {

QLabel *makeLabel(const QString &text, QWidget *parent = nullptr)
{
	auto *label = new QLabel(text, parent);
	label->setWordWrap(true);
	return label;
}

QFrame *makeCard(QWidget *parent)
{
	auto *card = new QFrame(parent);
	card->setObjectName(QStringLiteral("moonlitCard"));
	card->setFrameShape(QFrame::StyledPanel);
	return card;
}

} // namespace

MoonLitDashboard::MoonLitDashboard(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("moonlitDashboard"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	setStyleSheet(QStringLiteral(R"(
        #moonlitDashboard {
            background: #111318;
            color: #f2f4f8;
        }
        #moonlitCard {
            background: #1b1e25;
            border: 1px solid #2b303b;
            border-radius: 12px;
        }
        QLabel#moonlitTitle {
            color: #ffffff;
            font-size: 26px;
            font-weight: 700;
        }
        QLabel#moonlitSubtitle, QLabel#moonlitHint, QLabel#moonlitDetail {
            color: #9ba3b4;
        }
        QLabel#moonlitState {
            color: #83d89b;
            font-size: 17px;
            font-weight: 600;
        }
        QPushButton {
            min-height: 38px;
            padding: 0 16px;
            border: 1px solid #343b49;
            border-radius: 8px;
            background: #252a34;
            color: #f2f4f8;
        }
        QPushButton:hover {
            background: #303746;
        }
        QPushButton:disabled {
            color: #697180;
            background: #1d2027;
        }
        QPushButton#moonlitPrimary {
            border: 0;
            background: #7667f5;
            font-weight: 600;
        }
        QPushButton#moonlitPrimary:hover {
            background: #887bf7;
        }
    )"));

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(32, 28, 32, 28);
	root->setSpacing(18);

	auto *header = new QVBoxLayout();
	header->setSpacing(4);
	auto *title = makeLabel(QStringLiteral("MoonLit"), this);
	title->setObjectName(QStringLiteral("moonlitTitle"));
	auto *subtitle = makeLabel(QStringLiteral("Clips locales, sin escenas ni controles innecesarios"), this);
	subtitle->setObjectName(QStringLiteral("moonlitSubtitle"));
	header->addWidget(title);
	header->addWidget(subtitle);
	root->addLayout(header);

	auto *statusCard = makeCard(this);
	auto *statusLayout = new QVBoxLayout(statusCard);
	statusLayout->setContentsMargins(20, 18, 20, 18);
	statusLayout->setSpacing(10);

	stateLabel = makeLabel(QStringLiteral("Buffer detenido"), statusCard);
	stateLabel->setObjectName(QStringLiteral("moonlitState"));
	gameLabel = makeLabel(QStringLiteral("Sin juego detectado"), statusCard);
	gameLabel->setObjectName(QStringLiteral("moonlitDetail"));
	captureLabel = makeLabel(QStringLiteral("Captura: esperando configuracion"), statusCard);
	captureLabel->setObjectName(QStringLiteral("moonlitDetail"));
	encoderLabel = makeLabel(QStringLiteral("Encoder: se enumerara desde OBS"), statusCard);
	encoderLabel->setObjectName(QStringLiteral("moonlitDetail"));

	statusLayout->addWidget(stateLabel);
	statusLayout->addWidget(gameLabel);
	statusLayout->addWidget(captureLabel);
	statusLayout->addWidget(encoderLabel);
	root->addWidget(statusCard);

	root->addStretch(1);

	auto *actions = new QHBoxLayout();
	actions->setSpacing(10);
	replayButton = new QPushButton(QStringLiteral("Iniciar buffer"), this);
	replayButton->setObjectName(QStringLiteral("moonlitPrimary"));
	saveButton = new QPushButton(QStringLiteral("Guardar clip"), this);
	saveButton->setEnabled(false);
	auto *libraryButton = new QPushButton(QStringLiteral("Biblioteca"), this);
	auto *settingsButton = new QPushButton(QStringLiteral("Configuracion"), this);
	actions->addWidget(replayButton, 2);
	actions->addWidget(saveButton, 1);
	actions->addWidget(libraryButton, 1);
	actions->addWidget(settingsButton, 1);
	root->addLayout(actions);

	hintLabel = makeLabel(QStringLiteral("El buffer se inicia al detectar un juego compatible."), this);
	hintLabel->setObjectName(QStringLiteral("moonlitHint"));
	hintLabel->setAlignment(Qt::AlignCenter);
	root->addWidget(hintLabel);

	connect(replayButton, &QPushButton::clicked, this, &MoonLitDashboard::replayActionRequested);
	connect(saveButton, &QPushButton::clicked, this, &MoonLitDashboard::saveClipRequested);
	connect(libraryButton, &QPushButton::clicked, this, &MoonLitDashboard::libraryRequested);
	connect(settingsButton, &QPushButton::clicked, this, &MoonLitDashboard::settingsRequested);
}

void MoonLitDashboard::setReplayState(bool active, bool stopping)
{
	if (stopping) {
		stateLabel->setText(QStringLiteral("Deteniendo buffer..."));
		replayButton->setEnabled(false);
		return;
	}

	stateLabel->setText(active ? QStringLiteral("Buffer activo") : QStringLiteral("Buffer detenido"));
	replayButton->setText(active ? QStringLiteral("Detener buffer") : QStringLiteral("Iniciar buffer"));
	replayButton->setEnabled(true);
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
