/******************************************************************************
    MoonLit timeline strip

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "TimelineStrip.hpp"

#include "MoonLitTheme.hpp"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMarginX = 6;
constexpr int kTrackTop = 4;
constexpr int kSegmentGap = 2;

double msToX(qint64 ms, qint64 totalMs, int width)
{
	if (totalMs <= 0) {
		return 0;
	}
	const double usable = std::max(1, width - 2 * kMarginX);
	return kMarginX + usable * (static_cast<double>(ms) / static_cast<double>(totalMs));
}

} // namespace

TimelineStrip::TimelineStrip(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(kTrackHeight + 12);
	setMouseTracking(true);
}

void TimelineStrip::setSegments(const QVector<MoonLit::TimelineSegment> &segments,
				const QHash<QString, QImage> &thumbnails, qint64 totalDurationMs)
{
	segments_ = segments;
	thumbnails_ = thumbnails;
	totalDurationMs_ = std::max<qint64>(1, totalDurationMs);
	selectedIndex_ = std::clamp(selectedIndex_, -1, static_cast<int>(segments_.size()) - 1);
	dragMode_ = DragMode::None;
	dragIndex_ = -1;
	update();
}

void TimelineStrip::setSelected(int index)
{
	selectedIndex_ = index;
	update();
}

/* Positions are derived locally from the source lengths (a cumulative pass)
 * instead of trusting the model's timelineStartMs, so stale or unresolved
 * segments can never produce overlapping rects. */
QRect TimelineStrip::segmentRect(int index) const
{
	if (index < 0 || index >= segments_.size()) {
		return {};
	}
	qint64 position = 0;
	for (int previous = 0; previous < index; ++previous) {
		position += std::max<qint64>(kMinSegmentMs, segments_.at(previous).sourceLengthMs());
	}
	const qint64 length = std::max<qint64>(kMinSegmentMs, segments_.at(index).sourceLengthMs());
	const int xStart = static_cast<int>(std::round(msToX(position, totalDurationMs_, width())));
	const int xEnd = static_cast<int>(std::round(msToX(position + length, totalDurationMs_, width())));
	return QRect(xStart, kTrackTop, std::max(4, xEnd - xStart), kTrackHeight);
}

int TimelineStrip::indexAt(int x) const
{
	for (int index = segments_.size() - 1; index >= 0; --index) {
		if (segmentRect(index).contains(x, kTrackTop + kTrackHeight / 2)) {
			return index;
		}
	}
	return -1;
}

void TimelineStrip::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event)
	using namespace MoonLitTheme;
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, false);

	painter.fillRect(QRect(0, kTrackTop, width(), kTrackHeight), QColor(0x21, 0x22, 0x2c));

	for (int index = 0; index < segments_.size(); ++index) {
		QRect rect = segmentRect(index);
		rect.setWidth(std::max(2, rect.width() - kSegmentGap));
		const bool selected = index == selectedIndex_;

		QImage image = thumbnails_.value(segments_.at(index).clipId);
		if (!image.isNull()) {
			const QRect target = rect.adjusted(1, 1, -1, -1);
			painter.drawImage(target, image.scaled(target.size(), Qt::KeepAspectRatioByExpanding,
							       Qt::SmoothTransformation),
					  QRect(0, 0, image.width(), image.height()));
			painter.fillRect(target, QColor(0, 0, 0, 90));
		} else {
			painter.fillRect(rect.adjusted(1, 1, -1, -1), bgSurface());
		}

		painter.setPen(selected ? QPen(accent(), 2) : QPen(border(), 1));
		painter.drawRect(rect);

		if (selected && segments_.size() > 1) {
			painter.fillRect(QRect(rect.left(), kTrackTop, kHandleWidth, kTrackHeight), textMuted());
			painter.fillRect(QRect(rect.right() - kHandleWidth + 1, kTrackTop, kHandleWidth, kTrackHeight),
					 textMuted());
		}
	}
}

void TimelineStrip::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::MiddleButton) {
		dragMode_ = DragMode::Move;
		dragIndex_ = indexAt(event->pos().x());
		dragStartX_ = event->pos().x();
		return;
	}
	if (event->button() != Qt::LeftButton) {
		return;
	}

	const int index = indexAt(event->pos().x());
	if (index < 0) {
		return;
	}
	selectedIndex_ = index;
	emit segmentSelected(index);
	update();

	const QRect rect = segmentRect(index);
	const MoonLit::TimelineSegment &segment = segments_.at(index);
	if (event->pos().x() >= rect.right() - kHandleWidth && segments_.size() > 1) {
		dragMode_ = DragMode::TrimEnd;
		dragIndex_ = index;
		dragOriginalStartMs_ = segment.sourceStartMs;
		dragOriginalEndMs_ = segment.sourceEndMs;
		dragStartX_ = event->pos().x();
	} else if (event->pos().x() <= rect.left() + kHandleWidth && segments_.size() > 1) {
		dragMode_ = DragMode::TrimStart;
		dragIndex_ = index;
		dragOriginalStartMs_ = segment.sourceStartMs;
		dragOriginalEndMs_ = segment.sourceEndMs;
		dragStartX_ = event->pos().x();
	}
}

void TimelineStrip::mouseMoveEvent(QMouseEvent *event)
{
	if (dragMode_ == DragMode::None || dragIndex_ < 0) {
		return;
	}

	MoonLit::TimelineSegment &segment = segments_[dragIndex_];
	const qint64 originalLength = dragOriginalEndMs_ >= 0 ? dragOriginalEndMs_ - dragOriginalStartMs_ : 0;
	const double msPerPixel = static_cast<double>(totalDurationMs_) / std::max(1, width() - 2 * kMarginX);
	const qint64 deltaMs = static_cast<qint64>(std::llround((event->pos().x() - dragStartX_) * msPerPixel));

	qint64 newStart = segment.sourceStartMs;
	qint64 newEnd = segment.sourceEndMs;
	if (dragMode_ == DragMode::TrimEnd) {
		newStart = dragOriginalStartMs_;
		newEnd = std::max(dragOriginalStartMs_ + kMinSegmentMs, dragOriginalEndMs_ + deltaMs);
	} else if (dragMode_ == DragMode::TrimStart) {
		newStart = std::clamp<qint64>(dragOriginalStartMs_ + deltaMs, 0,
					     dragOriginalEndMs_ - kMinSegmentMs);
		newEnd = dragOriginalEndMs_;
	}

	if (newStart != segment.sourceStartMs || newEnd != segment.sourceEndMs) {
		segment.sourceStartMs = newStart;
		segment.sourceEndMs = newEnd;
		emit segmentTrimRequested(dragIndex_, newStart, newEnd);
		update();
	}
}

void TimelineStrip::mouseReleaseEvent(QMouseEvent *event)
{
	if (dragMode_ == DragMode::Move && dragIndex_ >= 0) {
		const int from = dragIndex_;
		const int to = std::clamp(indexAt(event->pos().x()), 0, static_cast<int>(segments_.size()) - 1);
		if (to != from) {
			emit segmentMoveRequested(from, to);
		}
	}
	dragMode_ = DragMode::None;
	dragIndex_ = -1;
}
