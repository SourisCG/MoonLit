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

#pragma once

#include <QWidget>

#include <moonlit/Clip.hpp>

class QGridLayout;
class QLabel;
class QPushButton;
class QTimer;
class MoonLitMixer;
class MoonLitRecordButton;
class MoonLitThumbCard;

class MoonLitDashboard final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitDashboard(QWidget *parent = nullptr);

	void setReplayState(bool active, bool stopping = false);
	void setDetectedGame(const QString &name);
	void setCaptureStatus(const QString &status);
	void setEncoderStatus(const QString &status);
	void setClipSaved(const QString &path);
	void setClipError(const QString &message);
	void setRecentClips(const QVector<MoonLit::Clip> &clips);
	/* Reflects the capture mode in the mode buttons without re-emitting. */
	void setFullscreenActive(bool active);
	MoonLitMixer *mixer() const { return mixer_; }

signals:
	void replayActionRequested();
	void saveClipRequested();
	void settingsRequested();
	void libraryRequested();
	void recentClipRequested(const QString &id, const QString &path);
	void fullscreenModeRequested(bool enabled);
	void gamePickRequested();

private:
	void rebuildRecentClips();
	void reflowRecentClips();

protected:
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;

private:
	QLabel *stateLabel = nullptr;
	QLabel *gameLabel = nullptr;
	QLabel *captureLabel = nullptr;
	QLabel *encoderLabel = nullptr;
	QLabel *clipNoticeLabel = nullptr;
	MoonLitRecordButton *recordButton = nullptr;
	QPushButton *saveButton = nullptr;
	QPushButton *folderButton = nullptr;
	QPushButton *autoModeButton = nullptr;
	QPushButton *fullscreenButton = nullptr;
	QPushButton *pickGameButton = nullptr;
	QTimer *noticeTimer = nullptr;
	QGridLayout *recentGrid = nullptr;
	QVector<MoonLit::Clip> recentClips_;
	QVector<MoonLitThumbCard *> recentCards_;
	MoonLitMixer *mixer_ = nullptr;
};
