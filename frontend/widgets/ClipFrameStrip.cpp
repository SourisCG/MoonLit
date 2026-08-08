#include "ClipFrameStrip.hpp"

#include "MoonLitTheme.hpp"

#include <QMouseEvent>
#include <QPainter>

ClipFrameStrip::ClipFrameStrip(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(60);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setCursor(Qt::PointingHandCursor);
}

void ClipFrameStrip::setFrames(const QVector<QImage> &frames, qint64 durationMs)
{
	frames_ = frames;
	durationMs_ = durationMs;
	update();
}

void ClipFrameStrip::setTrim(qint64 startMs, qint64 endMs)
{
	startMs_ = startMs;
	endMs_ = endMs;
	update();
}

int ClipFrameStrip::positionToX(qint64 positionMs) const
{
	if (durationMs_ <= 0) {
		return 0;
	}
	return static_cast<int>(positionMs * (width() - 2) / durationMs_) + 1;
}

qint64 ClipFrameStrip::xToPosition(int x) const
{
	if (durationMs_ <= 0 || width() <= 2) {
		return 0;
	}
	return std::max<qint64>(0, std::min<qint64>(durationMs_, (x - 1) * durationMs_ / (width() - 2)));
}

void ClipFrameStrip::updateTrimFromMouse(int x)
{
	qint64 position = xToPosition(x);
	if (dragging_ == 1) {
		const qint64 end = endMs_ >= 0 ? endMs_ : durationMs_;
		startMs_ = std::max<qint64>(0, std::min(position, end - 100));
		emit trimChanged(startMs_, endMs_);
	} else if (dragging_ == 2) {
		endMs_ = std::max(position, startMs_ + 100);
		emit trimChanged(startMs_, endMs_);
	}
	update();
}

void ClipFrameStrip::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.fillRect(rect(), MoonLitTheme::bgSurface());

	if (frames_.isEmpty() || durationMs_ <= 0) {
		painter.setPen(MoonLitTheme::textMuted());
		painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Cargando vista previa..."));
		return;
	}

	const int stripHeight = height() - 12;
	const qint64 end = endMs_ >= 0 ? endMs_ : durationMs_;
	const int startX = positionToX(startMs_);
	const int endX = positionToX(end);

	for (int index = 0; index < frames_.size(); ++index) {
		const qint64 frameStart = index * durationMs_ / frames_.size();
		const qint64 frameEnd = (index + 1) * durationMs_ / frames_.size();
		const int left = positionToX(frameStart);
		const int right = std::max(left + 1, positionToX(frameEnd));
		QImage scaled = frames_[index].scaledToHeight(stripHeight, Qt::SmoothTransformation);
		const int drawWidth = std::min(scaled.width(), right - left);
		const int drawX = left + (right - left - drawWidth) / 2;
		painter.drawImage(drawX, 6, scaled, 0, 0, drawWidth, stripHeight);
	}

	/* Dim the parts outside the trim range. */
	painter.fillRect(QRect(1, 6, startX - 1, stripHeight), QColor(0, 0, 0, 140));
	painter.fillRect(QRect(endX, 6, width() - endX - 1, stripHeight), QColor(0, 0, 0, 140));

	/* Trim handles. */
	painter.setPen(Qt::NoPen);
	painter.setBrush(MoonLitTheme::rec());
	painter.drawRect(QRect(startX - kHandleWidth / 2, 6, kHandleWidth, stripHeight));
	painter.drawRect(QRect(endX - kHandleWidth / 2, 6, kHandleWidth, stripHeight));
	painter.setPen(MoonLitTheme::text());
	painter.drawLine(QPointF(startX, 6), QPointF(startX, 6 + stripHeight));
	painter.drawLine(QPointF(endX, 6), QPointF(endX, 6 + stripHeight));
}

void ClipFrameStrip::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		return;
	}

	const int x = event->position().x();
	const int startX = positionToX(startMs_);
	const int endX = positionToX(endMs_ >= 0 ? endMs_ : durationMs_);
	if (std::abs(x - startX) <= kHandleGrab) {
		dragging_ = 1;
	} else if (std::abs(x - endX) <= kHandleGrab) {
		dragging_ = 2;
	} else {
		emit seekRequested(xToPosition(x));
	}
}

void ClipFrameStrip::mouseMoveEvent(QMouseEvent *event)
{
	if (dragging_ != 0) {
		updateTrimFromMouse(event->position().x());
	}
}

void ClipFrameStrip::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton && dragging_ != 0) {
		updateTrimFromMouse(event->position().x());
		dragging_ = 0;
	}
}
