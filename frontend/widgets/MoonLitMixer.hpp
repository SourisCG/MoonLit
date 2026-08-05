#pragma once

#include <QWidget>

#include <obs.h>
#include <util/config-file.h>

#include <QString>
#include <QVector>

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

/* Compact Medal-style mixer: one row per audio source with a volume slider
 * and a mute toggle, wired through the native OBS source API. Levels are
 * persisted in config (MoonLit.MixerVolume* and MoonLit.MixerMute*) and
 * applied when the source is created, so rows stay populated and stable
 * across game attach/detach. Rows with no live source render disabled
 * placeholders. */
class MoonLitMixer final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitMixer(QWidget *parent = nullptr);
	~MoonLitMixer() override;

	void setConfig(config_t *config) { config_ = config; }

	void clearSources();
	void addSource(const QString &name, obs_source_t *source);

private:
	void syncVolumes();
	void persist(const QString &name, int volume, bool muted);

	struct Row {
		QLabel *label = nullptr;
		QSlider *slider = nullptr;
		QPushButton *mute = nullptr;
		obs_source_t *source = nullptr;
		QString name;
	};

	QVector<Row> rows_;
	QTimer *syncTimer_ = nullptr;
	config_t *config_ = nullptr;
};
