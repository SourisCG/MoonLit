/******************************************************************************
    MoonLit timeline strip

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <moonlit/editor/Timeline.hpp>

#include <QHash>
#include <QImage>
#include <QPoint>
#include <QWidget>

/* Single-track timeline view. Segments are drawn proportionally to their
 * length with their thumbnail (or a placeholder block). Interactions:
 * left click selects, dragging the left/right edge trims the source range,
 * middle-button drag reorders. */
class TimelineStrip final : public QWidget {
	Q_OBJECT

public:
	explicit TimelineStrip(QWidget *parent = nullptr);

	void setSegments(const QVector<MoonLit::TimelineSegment> &segments,
			 const QHash<QString, QImage> &thumbnails, qint64 totalDurationMs);
	void setSelected(int index);

	int selectedIndex() const { return selectedIndex_; }

signals:
	void segmentSelected(int index);
	void segmentTrimRequested(int index, qint64 newSourceStartMs, qint64 newSourceEndMs);
	void segmentMoveRequested(int fromIndex, int toIndex);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	static constexpr int kHandleWidth = 8;
	static constexpr int kMinSegmentMs = 100;
	static constexpr int kTrackHeight = 72;

	int indexAt(int x) const;
	QRect segmentRect(int index) const;

	QVector<MoonLit::TimelineSegment> segments_;
	QHash<QString, QImage> thumbnails_;
	qint64 totalDurationMs_ = 0;
	int selectedIndex_ = -1;

	enum class DragMode { None, TrimStart, TrimEnd, Move };
	DragMode dragMode_ = DragMode::None;
	int dragIndex_ = -1;
	qint64 dragOriginalStartMs_ = 0;
	qint64 dragOriginalEndMs_ = -1;
	int dragStartX_ = 0;
};
