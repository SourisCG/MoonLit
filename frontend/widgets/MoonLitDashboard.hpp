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

class QLabel;
class QPushButton;
class QTimer;

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

signals:
	void replayActionRequested();
	void saveClipRequested();
	void settingsRequested();
	void libraryRequested();

private:
	QLabel *stateLabel = nullptr;
	QLabel *gameLabel = nullptr;
	QLabel *captureLabel = nullptr;
	QLabel *encoderLabel = nullptr;
	QLabel *clipNoticeLabel = nullptr;
	QLabel *hintLabel = nullptr;
	QPushButton *replayButton = nullptr;
	QPushButton *saveButton = nullptr;
	QTimer *noticeTimer = nullptr;
};
