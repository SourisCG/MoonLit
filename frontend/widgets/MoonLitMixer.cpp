#include "MoonLitMixer.hpp"

#include "MoonLitTheme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

MoonLitMixer::MoonLitMixer(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);

	using namespace MoonLitTheme;
	setStyleSheet(QStringLiteral(
		"QLabel { color: %1; }"
		"QPushButton { min-height: 26px; padding: 0 10px; border: 1px solid %2;"
		" border-radius: 6px; background: %3; color: %4; }"
		"QPushButton:hover { border-color: %5; }"
		"QPushButton:checked { background: %6; border-color: %6; color: #ffffff; }"
		"QSlider::groove:horizontal { height: 4px; background: %2; border-radius: 2px; }"
		"QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px;"
		" background: %5; }")
				      .arg(css(textMuted()), css(border()), css(bgSurface()), css(text()),
					   css(accent()), css(rec())));

	syncTimer_ = new QTimer(this);
	syncTimer_->setInterval(400);
	connect(syncTimer_, &QTimer::timeout, this, &MoonLitMixer::syncVolumes);
	syncTimer_->start();
}

MoonLitMixer::~MoonLitMixer()
{
	clearSources();
}

void MoonLitMixer::clearSources()
{
	while (QLayoutItem *item = layout()->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	rows_.clear();
}

void MoonLitMixer::addSource(const QString &name, obs_source_t *source)
{
	if (!source) {
		return;
	}

	auto *row = new QHBoxLayout();
	auto *label = new QLabel(name, this);
	label->setFixedWidth(110);
	auto *slider = new QSlider(Qt::Horizontal, this);
	slider->setRange(0, 100);
	slider->setValue(qRound(obs_source_get_volume(source) * 100.0));
	auto *mute = new QPushButton(QStringLiteral("M"), this);
	mute->setCheckable(true);
	mute->setToolTip(QStringLiteral("Silenciar"));
	mute->setChecked(obs_source_muted(source));
	mute->setFixedWidth(36);

	connect(slider, &QSlider::valueChanged, this, [source](int value) {
		obs_source_set_volume(source, static_cast<float>(value) / 100.0f);
	});
	connect(mute, &QPushButton::toggled, this, [source](bool checked) {
		obs_source_set_muted(source, checked);
	});

	row->addWidget(label);
	row->addWidget(slider, 1);
	row->addWidget(mute);
	layout()->addItem(row);

	rows_.append(Row{label, slider, mute, source});
}

void MoonLitMixer::syncVolumes()
{
	for (Row &row : rows_) {
		if (!row.source) {
			continue;
		}
		row.slider->blockSignals(true);
		row.slider->setValue(qRound(obs_source_get_volume(row.source) * 100.0));
		row.slider->blockSignals(false);
		row.mute->blockSignals(true);
		row.mute->setChecked(obs_source_muted(row.source));
		row.mute->blockSignals(false);
	}
}
