#pragma once

#include <QImage>
#include <QWidget>

#include <QVector>

/* Contact sheet of clip frames with two draggable trim markers. Clicking the
 * middle of the strip seeks; dragging near either edge moves the trim range. */
class ClipFrameStrip final : public QWidget {
	Q_OBJECT

public:
	explicit ClipFrameStrip(QWidget *parent = nullptr);

	void setFrames(const QVector<QImage> &frames, qint64 durationMs);
	void setTrim(qint64 startMs, qint64 endMs);

signals:
	void seekRequested(qint64 positionMs);
	void trimChanged(qint64 startMs, qint64 endMs);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	static constexpr int kHandleWidth = 10;
	static constexpr int kHandleGrab = 14;

	int positionToX(qint64 positionMs) const;
	qint64 xToPosition(int x) const;
	void updateTrimFromMouse(int x);

	QVector<QImage> frames_;
	qint64 durationMs_ = 0;
	qint64 startMs_ = 0;
	qint64 endMs_ = -1;
	int dragging_ = 0; /* 1 = start, 2 = end, 0 = none */
};
