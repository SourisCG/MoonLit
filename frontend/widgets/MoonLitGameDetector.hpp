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

#include <moonlit/capture/WindowsProcessUtil.hpp>
#include <moonlit/capture/WindowsTarget.hpp>

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>

class MoonLitGameDetector final : public QObject {
	Q_OBJECT

public:
	explicit MoonLitGameDetector(QObject *parent = nullptr);

	void start();
	void stop();
	bool active() const { return activeTarget_.isValid(); }

	/* Remembered games (MoonLit.GameList) the detector should accept even
	 * when the executable is not under a known launcher path. */
	void setManualGameList(const QStringList &gameList) { gameList_ = gameList; }

signals:
	void targetDetected(const MoonLitTarget &target);
	void targetFocusChanged(bool focused);
	void targetLost();

private slots:
	void poll();

private:
	static bool sameIdentity(const MoonLitTarget &left, const MoonLitTarget &right);
	bool isLikelyGame(const MoonLitTarget &target) const;

	QTimer timer_;
	QElapsedTimer monotonicTimer_;
	MoonLitTarget pendingTarget_;
	MoonLitTarget activeTarget_;
	QStringList gameList_;
	qint64 pendingSinceMs_ = 0;
	bool targetFocused_ = false;
};
