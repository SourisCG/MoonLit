/******************************************************************************
    MoonLit timeline editor

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <moonlit/Clip.hpp>
#include <moonlit/editor/Timeline.hpp>

#include <QHash>
#include <QImage>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class TimelineStrip;

/* Simple single-track timeline editor: add clips from the library, trim
 * segment edges on the strip, mute/gain per segment, reorder by middle-drag.
 * The project is saved and exported through the library widget (persistent
 * queue). */
class MoonLitTimelineEditor final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitTimelineEditor(QWidget *parent = nullptr);

	void setClips(const QVector<MoonLit::Clip> &clips);
	void setProject(const MoonLit::TimelineProject &project);

signals:
	void backRequested();
	void saveRequested(const MoonLit::TimelineProject &project);
	void exportRequested(const QString &timelineId);
	void statusMessage(const QString &message, bool error);

private:
	void refreshStrip();
	void refreshDetails();
	void resolveSegmentEnds();
	qint64 clipDurationMs(const QString &clipId) const;
	int addIndexForClip(const QString &clipId) const;

	QVector<MoonLit::Clip> clips_;
	MoonLit::TimelineProject project_;
	QHash<QString, QImage> thumbnails_;

	TimelineStrip *strip_ = nullptr;
	QLineEdit *nameEdit_ = nullptr;
	QLabel *durationLabel_ = nullptr;
	QComboBox *addClipCombo_ = nullptr;
	QPushButton *addButton_ = nullptr;
	QPushButton *removeButton_ = nullptr;
	QCheckBox *muteCheck_ = nullptr;
	QSlider *gainSlider_ = nullptr;
	QLabel *gainValue_ = nullptr;
	QPushButton *saveButton_ = nullptr;
	QPushButton *exportButton_ = nullptr;
	QPushButton *newButton_ = nullptr;
	QPushButton *backButton_ = nullptr;
};

