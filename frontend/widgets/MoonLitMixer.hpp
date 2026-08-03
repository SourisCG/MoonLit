#pragma once

#include <QWidget>

#include <obs.h>

#include <QVector>

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

/* Compact Medal-style mixer: one row per audio source with a volume slider
 * and a mute toggle, wired through the native OBS source API. */
class MoonLitMixer final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitMixer(QWidget *parent = nullptr);
	~MoonLitMixer() override;

	void clearSources();
	void addSource(const QString &name, obs_source_t *source);

private:
	void syncVolumes();

	struct Row {
		QLabel *label = nullptr;
		QSlider *slider = nullptr;
		QPushButton *mute = nullptr;
		obs_source_t *source = nullptr;
	};

	QVector<Row> rows_;
	QTimer *syncTimer_ = nullptr;
};
