/******************************************************************************
    MoonLit shell integration

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
******************************************************************************/

#include "OBSBasic.hpp"
#include "MoonLitDashboard.hpp"

#include <QStringList>

#ifdef _WIN32
#include "MoonLitGameDetector.hpp"

#include <util/windows/window-helpers.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#endif

namespace {

QString availableVideoEncoders()
{
	QStringList names;
	const char *id = nullptr;

	for (size_t index = 0; obs_enum_encoder_types(index, &id); ++index) {
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO) {
			continue;
		}

		const QString encoder = QString::fromUtf8(id);
		QString name;
		if (encoder == QStringLiteral("obs_nvenc_h264_tex")) {
			name = QStringLiteral("NVENC H.264");
		} else if (encoder == QStringLiteral("obs_nvenc_hevc_tex")) {
			name = QStringLiteral("NVENC HEVC");
		} else if (encoder == QStringLiteral("obs_nvenc_av1_tex")) {
			name = QStringLiteral("NVENC AV1");
		} else if (encoder == QStringLiteral("obs_qsv11")) {
			name = QStringLiteral("QSV H.264");
		} else if (encoder == QStringLiteral("obs_qsv11_av1")) {
			name = QStringLiteral("QSV AV1");
		} else if (encoder == QStringLiteral("h264_texture_amf")) {
			name = QStringLiteral("AMF H.264");
		} else if (encoder == QStringLiteral("h265_texture_amf")) {
			name = QStringLiteral("AMF HEVC");
		} else if (encoder == QStringLiteral("av1_texture_amf")) {
			name = QStringLiteral("AMF AV1");
		} else if (encoder == QStringLiteral("obs_x264")) {
			name = QStringLiteral("x264");
		} else if (encoder == QStringLiteral("ffmpeg_svt_av1")) {
			name = QStringLiteral("SVT-AV1");
		} else if (encoder == QStringLiteral("ffmpeg_aom_av1")) {
			name = QStringLiteral("AOM AV1");
		}

		if (!name.isEmpty() && !names.contains(name)) {
			names.append(name);
		}
	}

	return names.isEmpty() ? QStringLiteral("ninguno detectado") : names.join(QStringLiteral(", "));
}

#ifdef _WIN32
std::string encodeWindowPart(QString value)
{
	value.replace(QStringLiteral("#"), QStringLiteral("#22"));
	value.replace(QStringLiteral(":"), QStringLiteral("#3A"));
	return value.toUtf8().toStdString();
}

std::string makeWindowSelector(const MoonLitTarget &target)
{
	return encodeWindowPart(target.title) + ":" + encodeWindowPart(target.windowClass) + ":" +
	       encodeWindowPart(target.executable);
}
#endif

} // namespace

void OBSBasic::InitializeMoonLitShell()
{
	if (!moonlitDashboard) {
		setWindowTitle(QStringLiteral("MoonLit"));
		setObjectName(QStringLiteral("MoonLitMainWindow"));

		moonlitDashboard = new MoonLitDashboard(this);
		ui->previewLayout->addWidget(moonlitDashboard);

		connect(moonlitDashboard, &MoonLitDashboard::replayActionRequested, this,
			[this]() { ReplayBufferActionTriggered(); });
		connect(moonlitDashboard, &MoonLitDashboard::saveClipRequested, this, &OBSBasic::ReplayBufferSave);
		connect(moonlitDashboard, &MoonLitDashboard::settingsRequested, this,
			[this]() { on_action_Settings_triggered(); });
		connect(moonlitDashboard, &MoonLitDashboard::libraryRequested, this, [this]() {
			moonlitDashboard->setCaptureStatus(QStringLiteral("biblioteca local pendiente"));
		});

		connect(this, &OBSBasic::ReplayBufStarted, this,
			[this]() { moonlitDashboard->setReplayState(true); });
		connect(this, &OBSBasic::ReplayBufStopping, this,
			[this]() { moonlitDashboard->setReplayState(true, true); });
		connect(this, &OBSBasic::ReplayBufStopped, this,
			[this]() { moonlitDashboard->setReplayState(false); });
	}

	/* OBS restores its normal docks after the constructor has run. Apply the
	 * MoonLit surface again once initialization has finished. */
	ui->previewContainer->hide();
	ui->previewDisabledWidget->hide();
	ui->contextContainer->hide();
	ui->statusbar->hide();
	menuBar()->hide();
	ui->scenesDock->hide();
	ui->sourcesDock->hide();
	ui->mixerDock->hide();
	ui->transitionsDock->hide();
	controlsDock->hide();
	statsDock->hide();

	moonlitDashboard->setReplayState(ReplayBufferActive());
	moonlitDashboard->setCaptureStatus(QStringLiteral("fuente OBS existente"));
	moonlitDashboard->setEncoderStatus(availableVideoEncoders());
}

#ifdef _WIN32
void OBSBasic::InitializeMoonLitDetection()
{
	if (moonlitDetector) {
		return;
	}

	moonlitDetector = new MoonLitGameDetector(this);
	connect(moonlitDetector, &MoonLitGameDetector::targetDetected, this,
		[this](const MoonLitTarget &target) { ConfigureMoonLitCapture(target); });
	connect(moonlitDetector, &MoonLitGameDetector::targetFocusChanged, this, [this](bool focused) {
		if (moonlitCaptureItem) {
			obs_sceneitem_set_visible(moonlitCaptureItem, focused);
		}
		if (moonlitDashboard) {
			moonlitDashboard->setCaptureStatus(focused ? QStringLiteral("WGC de ventana")
									 : QStringLiteral("pausada temporalmente (Alt+Tab)"));
		}
	});
	connect(moonlitDetector, &MoonLitGameDetector::targetLost, this, [this]() {
		if (moonlitDashboard) {
			moonlitDashboard->setDetectedGame(QString());
			moonlitDashboard->setCaptureStatus(QStringLiteral("juego cerrado"));
		}
		if (ReplayBufferActive()) {
			StopReplayBuffer();
		}
		ClearMoonLitCapture();
	});
	moonlitDetector->start();
}

void OBSBasic::ConfigureMoonLitCapture(const MoonLitTarget &target)
{
	if (!target.isValid()) {
		return;
	}

	ClearMoonLitCapture();

	OBSDataAutoRelease settings = obs_data_create();
	const std::string selector = makeWindowSelector(target);
	obs_data_set_string(settings, "window", selector.c_str());
	obs_data_set_int(settings, "method", 2); /* Windows Graphics Capture */
	obs_data_set_int(settings, "priority", WINDOW_PRIORITY_TITLE);
	obs_data_set_bool(settings, "cursor", false);
	obs_data_set_bool(settings, "compatibility", false);
	obs_data_set_bool(settings, "client_area", true);
	obs_data_set_bool(settings, "force_sdr", false);
	obs_data_set_bool(settings, "capture_audio", true);

	moonlitCaptureSource = obs_source_create("window_capture", "MoonLit Game", settings, nullptr);
	if (!moonlitCaptureSource) {
		blog(LOG_ERROR, "MoonLit: unable to create the WGC window source");
		if (moonlitDashboard) {
			moonlitDashboard->setCaptureStatus(QStringLiteral("no se pudo crear WGC"));
		}
		return;
	}

	OBSScene scene = GetCurrentScene();
	if (!scene) {
		blog(LOG_ERROR, "MoonLit: no active scene for automatic capture");
		moonlitCaptureSource = nullptr;
		return;
	}

	moonlitCaptureItem = obs_scene_add(scene, moonlitCaptureSource);
	if (!moonlitCaptureItem) {
		blog(LOG_ERROR, "MoonLit: unable to add the WGC source to the active scene");
		moonlitCaptureSource = nullptr;
		return;
	}
	obs_sceneitem_set_visible(moonlitCaptureItem, true);

	struct obs_video_info videoInfo = {};
	if (obs_get_video_info(&videoInfo)) {
		vec2 bounds;
		vec2_set(&bounds, static_cast<float>(videoInfo.base_width), static_cast<float>(videoInfo.base_height));
		obs_sceneitem_set_bounds_type(moonlitCaptureItem, OBS_BOUNDS_STRETCH);
		obs_sceneitem_set_bounds(moonlitCaptureItem, &bounds);
	}

	if (moonlitDashboard) {
		moonlitDashboard->setDetectedGame(target.executable);
		moonlitDashboard->setCaptureStatus(QStringLiteral("WGC de ventana"));
	}

	if (!ReplayBufferActive()) {
		StartReplayBuffer();
	}
}

void OBSBasic::ClearMoonLitCapture()
{
	if (moonlitCaptureItem) {
		obs_sceneitem_remove(moonlitCaptureItem);
		moonlitCaptureItem = nullptr;
	}
	moonlitCaptureSource = nullptr;
}
#endif
