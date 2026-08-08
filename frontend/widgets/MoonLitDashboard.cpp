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
#include "MoonLitThumbCard.hpp"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QShowEvent>
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

		const QColor ring = active_ ? MoonLitTheme::rec().lighter(118) : MoonLitTheme::rec();
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

/* The dashboard shows only the most recent clips, never the whole library. */
constexpr int kMaxRecentCards = 6;

QLabel *makeLabel(const QString &text, QWidget *parent = nullptr)
{
	auto *label = new QLabel(text, parent);
	label->setWordWrap(true);
	return label;
}

} // namespace

MoonLitDashboard::MoonLitDashboard(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("moonlitDashboard"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	setStyleSheet(QStringLiteral(R"(
        #moonlitDashboard {
            background: transparent;
            color: %1;
        }
        QLabel#moonlitTitle {
            color: #ffffff;
            font-size: 26px;
            font-weight: 700;
        }
        QLabel#moonlitSubtitle, QLabel#moonlitHint, QLabel#moonlitDetail,
        QLabel#moonlitSection {
            color: %2;
        }
        QLabel#moonlitSection {
            font-size: 13px;
            font-weight: 600;
        }
        QLabel#moonlitState {
            color: %3;
            font-size: 16px;
            font-weight: 600;
        }
        QPushButton {
            min-height: 38px;
            padding: 0 16px;
            border: 1px solid %4;
            border-radius: 8px;
            background: %5;
            color: %1;
            font-weight: 500;
        }
        QPushButton:hover {
            border-color: %6;
            background: %7;
        }
        QPushButton:pressed {
            background: %10;
        }
        QPushButton:disabled {
            color: %2;
            background: %7;
            border-color: %4;
        }
        QPushButton#moonlitPrimary {
            border: 0;
            background: %6;
            color: #ffffff;
            font-weight: 600;
        }
        QPushButton#moonlitPrimary:hover {
            background: %8;
        }
        QPushButton#moonlitPrimary:disabled {
            background: %7;
            color: %2;
        }
    )")
			.arg(css(text()), css(textMuted()), css(ok()), css(border()), css(bgSurface()),
			     css(accent()), css(bgElevated()), css(accentHover()), css(rec()), css(night())));

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(32, 24, 32, 24);
	root->setSpacing(16);

	/* Capture mode row: automatic game detection, full-screen capture or a
	 * manually pinned process (Medal-style). */
	auto *modeRow = new QHBoxLayout();
	modeRow->setSpacing(8);
	auto *modeHint = makeLabel(QStringLiteral("Capturar:"), this);
	modeHint->setObjectName(QStringLiteral("moonlitHint"));
	autoModeButton = new QPushButton(QStringLiteral("Juego automático"), this);
	autoModeButton->setCheckable(true);
	autoModeButton->setChecked(true);
	fullscreenButton = new QPushButton(QStringLiteral("Pantalla completa"), this);
	fullscreenButton->setCheckable(true);
	pickGameButton = new QPushButton(QStringLiteral("Seleccionar juego…"), this);
	modeRow->addWidget(modeHint);
	modeRow->addWidget(autoModeButton);
	modeRow->addWidget(fullscreenButton);
	modeRow->addWidget(pickGameButton);
	modeRow->addStretch(1);
	root->addLayout(modeRow);

	connect(autoModeButton, &QPushButton::toggled, this, [this](bool checked) {
		if (checked) {
			fullscreenButton->blockSignals(true);
			fullscreenButton->setChecked(false);
			fullscreenButton->blockSignals(false);
			emit fullscreenModeRequested(false);
		}
	});
	connect(fullscreenButton, &QPushButton::toggled, this, [this](bool checked) {
		if (checked) {
			autoModeButton->blockSignals(true);
			autoModeButton->setChecked(false);
			autoModeButton->blockSignals(false);
		}
		emit fullscreenModeRequested(checked);
	});
	connect(pickGameButton, &QPushButton::clicked, this, &MoonLitDashboard::gamePickRequested);

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

	/* Actions: save clip (primary); navigation lives in the left rail. */
	auto *actions = new QHBoxLayout();
	actions->setSpacing(10);
	saveButton = new QPushButton(QStringLiteral("Guardar clip"), this);
	saveButton->setObjectName(QStringLiteral("moonlitPrimary"));
	saveButton->setEnabled(false);
	actions->addStretch(1);
	actions->addWidget(saveButton, 2);
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
	auto *noticeRow = new QHBoxLayout();
	noticeRow->addStretch(1);
	noticeRow->addWidget(clipNoticeLabel);
	folderButton = new QPushButton(QStringLiteral("Cambiar carpeta…"), this);
	folderButton->setVisible(false);
	noticeRow->addWidget(folderButton);
	noticeRow->addStretch(1);
	root->addLayout(noticeRow);

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
	connect(folderButton, &QPushButton::clicked, this, &MoonLitDashboard::settingsRequested);

	noticeTimer = new QTimer(this);
	noticeTimer->setSingleShot(true);
	noticeTimer->setInterval(8000);
	connect(noticeTimer, &QTimer::timeout, this, [this]() {
		clipNoticeLabel->clear();
		folderButton->setVisible(false);
	});
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

void MoonLitDashboard::setFullscreenActive(bool active)
{
	autoModeButton->blockSignals(true);
	fullscreenButton->blockSignals(true);
	autoModeButton->setChecked(!active);
	fullscreenButton->setChecked(active);
	autoModeButton->blockSignals(false);
	fullscreenButton->blockSignals(false);
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
	clipNoticeLabel->setStyleSheet(
		QStringLiteral("color: %1; font-weight: 600;").arg(MoonLitTheme::css(MoonLitTheme::ok())));
	clipNoticeLabel->setText(QStringLiteral("Clip guardado: %1").arg(QFileInfo(path).fileName()));
	noticeTimer->start();
}

void MoonLitDashboard::setClipError(const QString &message)
{
	clipNoticeLabel->setStyleSheet(
		QStringLiteral("color: %1; font-weight: 600;").arg(MoonLitTheme::css(MoonLitTheme::rec())));
	clipNoticeLabel->setText(message);
	folderButton->setVisible(true);
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
	recentCards_.clear();
	reflowRecentClips();
}

void MoonLitDashboard::reflowRecentClips()
{
	/* The width can still be tiny while the startup layout settles (a card
	 * build there would collapse to one column); the floor keeps the first
	 * pass reasonable and every later resize/show self-heals the grid. */
	const int columns = std::clamp(std::max(width(), 400) / (150 + 8), 1, 9);
	const int count = std::min<int>({columns * 3, kMaxRecentCards, static_cast<int>(recentClips_.size())});

	/* Create cards that are missing (icons decode lazily at paint time, so
	 * this is cheap); existing cards are reused and only moved. */
	while (recentCards_.size() < count) {
		const int index = recentCards_.size();
		const MoonLit::Clip &clip = recentClips_[index];
		auto *card = new MoonLitThumbCard(this);
		card->setFixedSize(150, 100);
		card->setThumbnail(QFileInfo::exists(clip.thumbnailPath)
						   ? QPixmap(clip.thumbnailPath)
						   : QIcon(QStringLiteral(":/res/images/moonlit-icon.png")).pixmap(142, 80));
		card->setTitle(clip.title);
		card->setToolTip(clip.title);
		connect(card, &MoonLitThumbCard::clicked, this,
			[this, id = clip.id, path = clip.mediaPath]() { emit recentClipRequested(id, path); });
		recentCards_.append(card);
	}

	while (QLayoutItem *item = recentGrid->takeAt(0)) {
		delete item;
	}
	for (int index = 0; index < count; ++index) {
		recentGrid->addWidget(recentCards_.at(index), index / columns, index % columns);
		recentCards_.at(index)->show();
	}
	for (int index = count; index < recentCards_.size(); ++index) {
		recentCards_.at(index)->hide();
	}
	recentGrid->setColumnStretch(columns, 1);
}

void MoonLitDashboard::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	/* Reflow whenever the width changes, even before the widget is shown:
	 * cards built during the startup layout (when width() is still small)
	 * land in the right column count as soon as the real size lands. */
	if (!recentClips_.isEmpty() && event->size().width() != event->oldSize().width()) {
		reflowRecentClips();
	}
}

void MoonLitDashboard::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	/* Coming back from another view may reveal cards that were built while
	 * hidden (small width); rebuild them for the current size. */
	if (!recentClips_.isEmpty()) {
		reflowRecentClips();
	}
}

#include "MoonLitDashboard.moc"

