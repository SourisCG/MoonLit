#include "MoonLitMixer.hpp"

#include "MoonLitTheme.hpp"

#include <util/config-file.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

MoonLitMixer::MoonLitMixer(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);

	using namespace MoonLitTheme;
	setStyleSheet(QStringLiteral(
		"QLabel { color: %1; }"
		"QLabel:disabled { color: %2; }"
		"QPushButton { min-height: 26px; padding: 0 10px; border: 1px solid %3;"
		" border-radius: 6px; background: %4; color: %1; }"
		"QPushButton:hover { border-color: %5; }"
		"QPushButton:checked { background: %6; border-color: %6; color: #ffffff; }"
		"QSlider::groove:horizontal { height: 4px; background: %3; border-radius: 2px; }"
		"QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px;"
		" background: %5; }")
				      .arg(css(text()), css(textMuted()), css(border()), css(bgSurface()),
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
}void MoonLitMixer::addSource(const QString &name, obs_source_t *source)
{
	auto *row = new QHBoxLayout();
	auto *label = new QLabel(source ? name : QStringLiteral("%1 (sin fuente)").arg(name), this);
	/* Fixed heights instead of sizeHint: the OBS app-wide stylesheet makes
	 * QStyleSheetStyle report 0 sizeHints through QWidgetItem for widgets
	 * created before the theme settles, collapsing the whole mixer to
	 * height 0. Fixed min/max sizes survive that (the layout clamps
	 * geometry to them), so rows always render. */
	label->setFixedWidth(110);
	label->setFixedHeight(20);
	auto *slider = new QSlider(Qt::Horizontal, this);
	slider->setRange(0, 100);
	slider->setFixedHeight(22);
	auto *mute = new QPushButton(QStringLiteral("M"), this);
	mute->setCheckable(true);
	mute->setToolTip(QStringLiteral("Silenciar"));
	mute->setFixedWidth(36);
	mute->setFixedHeight(28);

	/* Rows are built while the window is still hidden (slow startup), so Qt
	 * creates the widgets hidden (WA_WState_Hidden); QWidgetItem::isEmpty()
	 * then reports them empty and their size hints become 0, collapsing the
	 * mixer. An explicit show() marks them visible so hints work once the
	 * window appears. */
	label->show();
	slider->show();
	mute->show();

	if (!source) {
		/* Placeholder row: the track exists but has no live source yet
		 * (e.g. game not running, chat exe not configured). */
		slider->setEnabled(false);
		mute->setEnabled(false);
		row->addWidget(label);
		row->addWidget(slider, 1);
		row->addWidget(mute);
		layout()->addItem(row);
		rows_.append(Row{label, slider, mute, nullptr, name});
		return;
	}

	slider->setValue(qRound(obs_source_get_volume(source) * 100.0));
	mute->setChecked(obs_source_muted(source));

	connect(slider, &QSlider::valueChanged, this, [this, name, source](int value) {
		obs_source_set_volume(source, static_cast<float>(value) / 100.0f);
		persist(name, value, obs_source_muted(source));
	});
	connect(mute, &QPushButton::toggled, this, [this, name, source](bool checked) {
		obs_source_set_muted(source, checked);
		persist(name, qRound(obs_source_get_volume(source) * 100.0f), checked);
	});

	row->addWidget(label);
	row->addWidget(slider, 1);
	row->addWidget(mute);
	layout()->addItem(row);

	rows_.append(Row{label, slider, mute, source, name});
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

void MoonLitMixer::persist(const QString &name, int volume, bool muted)
{
	if (!config_) {
		return;
	}
	const std::string volumeKey = ("MixerVolume" + name).toUtf8().constData();
	const std::string muteKey = ("MixerMute" + name).toUtf8().constData();
	config_set_int(config_, "MoonLit", volumeKey.c_str(), volume);
	config_set_bool(config_, "MoonLit", muteKey.c_str(), muted);
}
