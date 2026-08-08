/******************************************************************************
    MoonLit timeline editor

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MoonLitTimelineEditor.hpp"

#include "MoonLitTheme.hpp"
#include "TimelineStrip.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr qint64 kMinSegmentMs = 100;

} // namespace

MoonLitTimelineEditor::MoonLitTimelineEditor(QWidget *parent) : QWidget(parent)
{
	using namespace MoonLitTheme;
	setObjectName(QStringLiteral("moonlitTimelineEditor"));
	setStyleSheet(QStringLiteral(R"(
        #moonlitTimelineEditor { background: transparent; }
        QLabel#timelineTitle { color: #ffffff; font-size: 24px; font-weight: 700; }
        QLabel#timelineDetail { color: %1; }
        QLineEdit, QComboBox, QSpinBox { background: %2; color: %3; border: 1px solid %4; border-radius: 7px; padding: 6px; }
        QComboBox::drop-down { border: 0; }
        QComboBox QAbstractItemView { background: %5; color: %3; selection-background-color: %6; }
        QPushButton { min-height: 30px; padding: 0 10px; border: 1px solid %4; border-radius: 7px; background: %2; color: %3; font-weight: 500; }
        QPushButton:hover { background: %7; border-color: %6; }
        QPushButton:pressed { background: %5; }
        QPushButton:disabled { color: %1; background: %8; border-color: %4; }
    )")
			.arg(css(textMuted()), css(bgSurface()), css(text()), css(border()), css(bgElevated()),
			     css(accent()), css(bgElevated()), css(bgDeep())));

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(28, 24, 28, 24);
	root->setSpacing(12);

	auto *header = new QHBoxLayout();
	backButton_ = new QPushButton(QStringLiteral("Volver"), this);
	auto *title = new QLabel(QStringLiteral("Timeline"), this);
	title->setObjectName(QStringLiteral("timelineTitle"));
	newButton_ = new QPushButton(QStringLiteral("Nuevo"), this);
	saveButton_ = new QPushButton(QStringLiteral("Guardar"), this);
	exportButton_ = new QPushButton(QStringLiteral("Exportar"), this);
	header->addWidget(backButton_);
	header->addWidget(title);
	header->addStretch(1);
	header->addWidget(newButton_);
	header->addWidget(saveButton_);
	header->addWidget(exportButton_);
	root->addLayout(header);

	nameEdit_ = new QLineEdit(this);
	nameEdit_->setPlaceholderText(QStringLiteral("Nombre del timeline"));
	root->addWidget(nameEdit_);

	durationLabel_ = new QLabel(this);
	durationLabel_->setObjectName(QStringLiteral("timelineDetail"));
	root->addWidget(durationLabel_);

	strip_ = new TimelineStrip(this);
	root->addWidget(strip_);

	auto *segmentRow = new QHBoxLayout();
	auto *segmentLabel = new QLabel(QStringLiteral("Agregar clip:"), this);
	addClipCombo_ = new QComboBox(this);
	addButton_ = new QPushButton(QStringLiteral("Agregar"), this);
	removeButton_ = new QPushButton(QStringLiteral("Quitar"), this);
	segmentRow->addWidget(segmentLabel);
	segmentRow->addWidget(addClipCombo_, 1);
	segmentRow->addWidget(addButton_);
	segmentRow->addWidget(removeButton_);
	root->addLayout(segmentRow);

	auto *audioRow = new QHBoxLayout();
	auto *muteLabel = new QLabel(QStringLiteral("Segmento:"), this);
	muteCheck_ = new QCheckBox(QStringLiteral("Silenciar"), this);
	gainSlider_ = new QSlider(Qt::Horizontal, this);
	gainSlider_->setRange(-20, 20);
	gainSlider_->setValue(0);
	gainValue_ = new QLabel(QStringLiteral("0 dB"), this);
	gainValue_->setMinimumWidth(48);
	audioRow->addWidget(muteLabel);
	audioRow->addWidget(muteCheck_);
	audioRow->addWidget(gainSlider_, 1);
	audioRow->addWidget(gainValue_);
	root->addLayout(audioRow);

	root->addStretch(1);

	connect(backButton_, &QPushButton::clicked, this, &MoonLitTimelineEditor::backRequested);
	connect(newButton_, &QPushButton::clicked, this, [this]() {
		setProject(MoonLit::TimelineProject::create(QStringLiteral("Nuevo timeline")));
	});
	connect(saveButton_, &QPushButton::clicked, this, [this]() {
		project_.name = nameEdit_->text().trimmed();
		if (project_.name.isEmpty()) {
			emit statusMessage(QStringLiteral("El timeline necesita un nombre"), true);
			return;
		}
		emit saveRequested(project_);
	});
	connect(exportButton_, &QPushButton::clicked, this,
		[this]() { emit exportRequested(project_.id); });

	connect(strip_, &TimelineStrip::segmentSelected, this, [this](int index) {
		refreshDetails();
		strip_->setSelected(index);
	});
	connect(strip_, &TimelineStrip::segmentTrimRequested, this,
		[this](int index, qint64 startMs, qint64 endMs) {
			if (index < 0 || index >= project_.segments.size()) {
				return;
			}
			project_.segments[index].sourceStartMs = startMs;
			project_.segments[index].sourceEndMs = endMs;
			project_.recomputePositions();
			refreshStrip();
			refreshDetails();
		});
	connect(strip_, &TimelineStrip::segmentMoveRequested, this, [this](int fromIndex, int toIndex) {
		if (fromIndex < 0 || fromIndex >= project_.segments.size()) {
			return;
		}
		project_.segments.move(fromIndex, toIndex);
		project_.recomputePositions();
		refreshStrip();
		emit statusMessage(QStringLiteral("Segmento reordenado"), false);
	});
	connect(addButton_, &QPushButton::clicked, this, [this]() {
		const int index = addClipCombo_->currentData().toInt();
		if (index < 0 || index >= clips_.size()) {
			return;
		}
		const MoonLit::Clip &clip = clips_.at(index);
		if (clip.missing) {
			emit statusMessage(QStringLiteral("El clip seleccionado no esta disponible"), true);
			return;
		}
		MoonLit::TimelineSegment segment;
		segment.clipId = clip.id;
		segment.sourceStartMs = 0;
		segment.sourceEndMs = clip.metadata.durationMs > 0 ? clip.metadata.durationMs : -1;
		if (segment.sourceEndMs >= 0) {
			project_.segments.append(segment);
			project_.recomputePositions();
			refreshStrip();
			strip_->setSelected(project_.segments.size() - 1);
			refreshDetails();
		} else {
			emit statusMessage(QStringLiteral("No se pudo medir la duracion del clip"), true);
		}
	});
	connect(removeButton_, &QPushButton::clicked, this, [this]() {
		const int index = strip_->selectedIndex();
		if (index < 0 || index >= project_.segments.size()) {
			return;
		}
		project_.segments.removeAt(index);
		project_.recomputePositions();
		refreshStrip();
		refreshDetails();
	});
	connect(muteCheck_, &QCheckBox::toggled, this, [this](bool checked) {
		gainSlider_->setEnabled(!checked);
		if (strip_->selectedIndex() >= 0) {
			project_.segments[strip_->selectedIndex()].muted = checked;
		}
	});
	connect(gainSlider_, &QSlider::valueChanged, this, [this](int value) {
		gainValue_->setText(QStringLiteral("%1 dB").arg(value));
		if (strip_->selectedIndex() >= 0) {
			project_.segments[strip_->selectedIndex()].gainDb = value;
		}
	});
}

void MoonLitTimelineEditor::setClips(const QVector<MoonLit::Clip> &clips)
{
	clips_ = clips;
	addClipCombo_->clear();
	thumbnails_.clear();
	for (const MoonLit::Clip &clip : clips) {
		if (clip.missing) {
			continue;
		}
		QString text = clip.title;
		if (clip.metadata.durationMs > 0) {
			text += QStringLiteral(" (%1 s)").arg(clip.metadata.durationMs / 1000);
		}
		addClipCombo_->addItem(text, addIndexForClip(clip.id));
		if (QFileInfo::exists(clip.thumbnailPath)) {
			thumbnails_.insert(clip.id, QImage(clip.thumbnailPath));
		}
	}
	refreshStrip();
}

void MoonLitTimelineEditor::setProject(const MoonLit::TimelineProject &project)
{
	project_ = project;
	nameEdit_->setText(project_.name);
	strip_->setSelected(-1);
	resolveSegmentEnds();
	refreshStrip();
	refreshDetails();
}

void MoonLitTimelineEditor::resolveSegmentEnds()
{
	for (MoonLit::TimelineSegment &segment : project_.segments) {
		if (segment.sourceEndMs < 0) {
			const qint64 duration = clipDurationMs(segment.clipId);
			if (duration > 0) {
				segment.sourceEndMs = duration;
			}
		}
	}
	project_.recomputePositions();
}

qint64 MoonLitTimelineEditor::clipDurationMs(const QString &clipId) const
{
	for (const MoonLit::Clip &clip : clips_) {
		if (clip.id == clipId && clip.metadata.durationMs > 0) {
			return clip.metadata.durationMs;
		}
	}
	return -1;
}

int MoonLitTimelineEditor::addIndexForClip(const QString &clipId) const
{
	for (int index = 0; index < clips_.size(); ++index) {
		if (clips_.at(index).id == clipId) {
			return index;
		}
	}
	return -1;
}

void MoonLitTimelineEditor::refreshStrip()
{
	strip_->setSegments(project_.segments, thumbnails_, project_.durationMs());
}

void MoonLitTimelineEditor::refreshDetails()
{
	const int index = strip_->selectedIndex();
	if (index < 0 || index >= project_.segments.size()) {
		muteCheck_->setEnabled(false);
		gainSlider_->setEnabled(false);
		removeButton_->setEnabled(false);
		durationLabel_->setText(QStringLiteral("Duracion total: %1 s").arg(project_.durationMs() / 1000));
		return;
	}

	const MoonLit::TimelineSegment &segment = project_.segments.at(index);
	QSignalBlocker blockerMute(muteCheck_);
	QSignalBlocker blockerGain(gainSlider_);
	muteCheck_->setEnabled(true);
	muteCheck_->setChecked(segment.muted);
	gainSlider_->setEnabled(!segment.muted);
	gainSlider_->setValue(static_cast<int>(std::clamp(segment.gainDb, -20.0, 20.0)));
	gainValue_->setText(QStringLiteral("%1 dB").arg(gainSlider_->value()));
	removeButton_->setEnabled(true);
	durationLabel_->setText(QStringLiteral("Duracion total: %1 s | Segmento %2 de %3 (%4 s)")
					.arg(project_.durationMs() / 1000)
					.arg(index + 1)
					.arg(project_.segments.size())
					.arg(segment.sourceLengthMs() / 1000));
}
