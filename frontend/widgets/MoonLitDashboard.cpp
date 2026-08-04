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

#include "MoonLitMixer.hpp"
#include "MoonLitTheme.hpp"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

/* The record button is a custom-painted widget with no stylesheet at all:
 * Qt style sheets rewrite a widget's minimum/maximum sizes through their own
 * box model, which made the layout overlap the state labels with the button.
 * A plain painted widget keeps the exact 120x120 geometry in code. */
class MoonLitRecordButton final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitRecordButton(QWidget *parent = nullptr) : QWidget(parent)
	{
		setFixedSize(120, 120);
		setCursor(Qt::PointingHandCursor);
	}

	void setActive(bool active)
	{
		active_ = active;
		update();
	}

signals:
	void clicked();

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);

		const QColor ring = active_ ? QColor(0xff, 0x6e, 0x6e) : MoonLitTheme::rec();
		painter.setPen(QPen(ring, 3));
		painter.setBrush(active_ ? MoonLitTheme::rec() : MoonLitTheme::bgSurface());
		painter.drawEllipse(QRectF(1.5, 1.5, 117, 117));

		QFont font = painter.font();
		font.setPixelSize(22);
		font.setBold(true);
		painter.setFont(font);
		painter.setPen(active_ ? QColor(Qt::white) : ring);
		painter.drawText(rect(), Qt::AlignCenter, active_ ? QStringLiteral("\u25A0") : QStringLiteral("REC"));
	}

	void mousePressEvent(QMouseEvent *) override { emit clicked(); }

private:
	bool active_ = false;
};

namespace {

using namespace MoonLitTheme;

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
		"QPushButton { background: %1; border: 1px solid %2; border-radius: 8px; }"
		"QPushButton:hover { border-color: %3; }")
				      .arg(css(bgSurface()), css(border()), css(accent())));
	return button;
}

} // namespace

MoonLitDashboard::MoonLitDashboard(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("moonlitDashboard"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	setStyleSheet(QStringLiteral(R"(
        #moonlitDashboard {
            background: %1;
            color: %2;
        }
        QLabel#moonlitTitle {
            color: #ffffff;
            font-size: 26px;
            font-weight: 700;
        }
        QLabel#moonlitSubtitle, QLabel#moonlitHint, QLabel#moonlitDetail,
        QLabel#moonlitSection {
            color: %3;
        }
        QLabel#moonlitSection {
            font-size: 13px;
            font-weight: 600;
        }
        QLabel#moonlitState {
            color: %4;
            font-size: 16px;
            font-weight: 600;
        }
        QPushButton {
            min-height: 38px;
            padding: 0 16px;
            border: 1px solid %5;
            border-radius: 8px;
            background: %6;
            color: %2;
            font-weight: 500;
        }
        QPushButton:hover {
            border-color: %7;
            background: %8;
        }
        QPushButton:pressed {
            background: %9;
        }
        QPushButton:disabled {
            color: %3;
            background: %10;
            border-color: %11;
        }
        QPushButton#moonlitPrimary {
            border: 0;
            background: %7;
            color: #ffffff;
            font-weight: 600;
        }
        QPushButton#moonlitPrimary:hover {
            background: %12;
        }
        QPushButton#moonlitPrimary:disabled {
            background: %8;
            color: %3;
        }
    )")
			.arg(css(bgDeep()), css(text()), css(textMuted()), css(ok()), css(border()),
			     css(bgSurface()), css(accent()), css(bgElevated()), QColor(0x2f, 0x31, 0x42).name(),
			     QColor(0x24, 0x25, 0x2f).name(), QColor(0x3a, 0x3d, 0x4d).name(), css(accentHover()),
			     css(rec()), QColor(0xff, 0x6e, 0x6e).name()));

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

	/* Center: the big record button and the current state, stacked with
	 * explicit sizes so the labels can never overlap the button. */
	auto *center = new QVBoxLayout();
	center->setSpacing(12);
	center->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
	recordButton = new MoonLitRecordButton(this);
	center->addWidget(recordButton, 0, Qt::AlignHCenter);
	stateLabel = makeLabel(QStringLiteral("Buffer detenido"), this);
	stateLabel->setObjectName(QStringLiteral("moonlitState"));
	stateLabel->setAlignment(Qt::AlignCenter);
	stateLabel->setWordWrap(false);
	stateLabel->setFixedHeight(24);
	center->addWidget(stateLabel, 0, Qt::AlignHCenter);
	gameLabel = makeLabel(QStringLiteral("Sin juego detectado"), this);
	gameLabel->setObjectName(QStringLiteral("moonlitDetail"));
	gameLabel->setAlignment(Qt::AlignCenter);
	gameLabel->setWordWrap(false);
	gameLabel->setFixedHeight(22);
	center->addWidget(gameLabel, 0, Qt::AlignHCenter);
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

	/* Compact mixer for the audio channels. */
	auto *mixerTitle = makeLabel(QStringLiteral("Mezclador"), this);
	mixerTitle->setObjectName(QStringLiteral("moonlitSection"));
	root->addWidget(mixerTitle);
	mixer_ = new MoonLitMixer(this);
	root->addWidget(mixer_);
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

	connect(recordButton, &MoonLitRecordButton::clicked, this, &MoonLitDashboard::replayActionRequested);
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
	recordButton->setActive(active);
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

#include "MoonLitDashboard.moc"

