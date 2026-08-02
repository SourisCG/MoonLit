/******************************************************************************
    MoonLit foreground game detector

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

#include <QObject>
#include <QString>
#include <QTimer>

struct MoonLitTarget {
	quintptr window = 0;
	quint32 processId = 0;
	quint64 creationTime = 0;
	QString title;
	QString windowClass;
	QString executable;
	QString executablePath;

	bool isValid() const { return window != 0 && processId != 0 && creationTime != 0; }
};

Q_DECLARE_METATYPE(MoonLitTarget)

class MoonLitGameDetector final : public QObject {
	Q_OBJECT

public:
	explicit MoonLitGameDetector(QObject *parent = nullptr);

	void start();
	void stop();
	bool active() const { return activeTarget_.isValid(); }

signals:
	void targetDetected(const MoonLitTarget &target);
	void targetFocusChanged(bool focused);
	void targetLost();

private slots:
	void poll();

private:
	static bool readTarget(quintptr window, MoonLitTarget &target);
	static bool sameIdentity(const MoonLitTarget &left, const MoonLitTarget &right);
	static bool isLikelyGame(const MoonLitTarget &target);
	static bool isProcessAlive(const MoonLitTarget &target);

	QTimer timer_;
	MoonLitTarget pendingTarget_;
	MoonLitTarget activeTarget_;
	qint64 pendingSinceMs_ = 0;
	bool targetFocused_ = false;
};
