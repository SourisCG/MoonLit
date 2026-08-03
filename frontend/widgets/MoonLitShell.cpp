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
#include "MoonLitLibraryWidget.hpp"
#include "MoonLitMixer.hpp"

#include <moonlit/ui/MoonLitSettingsDialog.hpp>

#include <obs-hotkey.h>

#include <cstdint>
#include <QElapsedTimer>
#include <QStringList>
#include <QTimer>

#include <algorithm>

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

OBSSceneItem moonlitShieldItem;
OBSSceneItem moonlitAudioItem;
OBSSource moonlitAudioSource;
OBSSceneItem moonlitMicItem;
OBSSource moonlitMicSource;
OBSSceneItem moonlitChatItem;
OBSSource moonlitChatSource;
OBSSceneItem moonlitDesktopItem;
OBSSource moonlitDesktopSource;
MoonLitTarget moonlitTarget;
QElapsedTimer moonlitWgcTimer;
bool moonlitTargetFocused = false;
bool moonlitUsingMonitorFallback = false;
bool moonlitReplayStartRequested = false;
QElapsedTimer moonlitConfigureRetryTimer;
QElapsedTimer moonlitReplayRetryTimer;
int moonlitReplayStartFailures = 0;
bool moonlitReplayAutoBlocked = false;
bool moonlitReplayManualStopRequested = false;

bool monitorFallbackIsSafe(const MoonLitTarget &target)
{
	const HWND window = reinterpret_cast<HWND>(target.window);
	if (!IsWindow(window) || IsIconic(window) || !IsWindowVisible(window))
		return false;

	const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
	MONITORINFO info = {};
	info.cbSize = sizeof(info);
	RECT windowRect = {};
	return monitor && GetMonitorInfoW(monitor, &info) && GetWindowRect(window, &windowRect) &&
		EqualRect(&info.rcMonitor, &windowRect);
}

void setMoonLitBounds(obs_sceneitem_t *item)
{
	if (!item)
		return;

	struct obs_video_info videoInfo = {};
	if (!obs_get_video_info(&videoInfo))
		return;

	vec2 bounds;
	vec2_set(&bounds, static_cast<float>(videoInfo.base_width), static_cast<float>(videoInfo.base_height));
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);
	obs_sceneitem_set_bounds(item, &bounds);
}

void createMoonLitShield(OBSScene scene)
{
	if (!scene)
		return;

	struct obs_video_info videoInfo = {};
	if (!obs_get_video_info(&videoInfo))
		return;

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_int(settings, "color", 0xFF000000);
	obs_data_set_int(settings, "width", videoInfo.base_width);
	obs_data_set_int(settings, "height", videoInfo.base_height);

	OBSSourceAutoRelease shield =
		obs_source_create_private("color_source", "MoonLit Capture Shield", settings);
	if (!shield) {
		blog(LOG_ERROR, "MoonLit: unable to create the black capture shield");
		return;
	}

	moonlitShieldItem = obs_scene_add(scene, shield);
	if (moonlitShieldItem) {
		setMoonLitBounds(moonlitShieldItem);
		obs_sceneitem_set_visible(moonlitShieldItem, true);
		obs_sceneitem_set_locked(moonlitShieldItem, true);
	}
}

void setMoonLitCaptureCovered(obs_sceneitem_t *captureItem)
{
	if (moonlitShieldItem) {
		obs_sceneitem_set_visible(moonlitShieldItem, true);
		if (captureItem)
			obs_sceneitem_set_visible(captureItem, true);
	} else if (captureItem) {
		obs_sceneitem_set_visible(captureItem, false);
	}
}

void setMoonLitCaptureShielded(obs_sceneitem_t *captureItem, bool shielded)
{
	if (shielded) {
		if (captureItem)
			obs_sceneitem_set_visible(captureItem, false);
		if (moonlitShieldItem)
			obs_sceneitem_set_visible(moonlitShieldItem, true);
		return;
	}

	if (!moonlitShieldItem) {
		if (captureItem)
			obs_sceneitem_set_visible(captureItem, false);
		return;
	}

	/* Reveal only after the shield is already covering the source. */
	obs_sceneitem_set_visible(moonlitShieldItem, true);
	if (captureItem)
		obs_sceneitem_set_visible(captureItem, true);
	obs_sceneitem_set_visible(moonlitShieldItem, false);
}

obs_source_t *createMoonLitAudioSource(const std::string &selector)
{
	static const char *const audioType = "wasapi_process_output_capture";
	if (!obs_get_latest_input_type_id(audioType))
		return nullptr;

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "window", selector.c_str());
	obs_data_set_int(settings, "priority", WINDOW_PRIORITY_EXE);
	obs_data_set_int(settings, "moonlit_hwnd", static_cast<int64_t>(moonlitTarget.window));
	obs_data_set_int(settings, "moonlit_process_id", moonlitTarget.processId);
	obs_data_set_int(settings, "moonlit_creation_time", static_cast<int64_t>(moonlitTarget.creationTime));
	obs_source_t *source = obs_source_create_private(audioType, "MoonLit Game Audio", settings);
	if (source) {
		/* Track 2: game only. */
		obs_source_set_audio_mixers(source, (1u << 1));
	}
	return source;
}

/* Track 3: explicit microphone with persisted device. */
obs_source_t *createMoonLitMicSource()
{
	static const char *const audioType = "wasapi_input_capture";
	if (!obs_get_latest_input_type_id(audioType))
		return nullptr;

	const char *deviceId = config_get_string(OBSBasic::Get()->Config(), "MoonLit", "MicDeviceId");
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "device_id", deviceId && *deviceId ? deviceId : "default");

	obs_source_t *source = obs_source_create_private(audioType, "MoonLit Micrófono", settings);
	if (source) {
		obs_source_set_audio_mixers(source, (1u << 2));
	}
	return source;
}

/* Track 4: chat process audio (e.g. Discord) with exe-based restart recovery. */
obs_source_t *createMoonLitChatSource()
{
	const char *chatExe = config_get_string(OBSBasic::Get()->Config(), "MoonLit", "ChatExe");
	if (!chatExe || !*chatExe)
		return nullptr;

	static const char *const audioType = "wasapi_process_output_capture";
	if (!obs_get_latest_input_type_id(audioType))
		return nullptr;

	std::string selector = "::" + encodeWindowPart(QString::fromUtf8(chatExe));
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "window", selector.c_str());
	obs_data_set_int(settings, "priority", WINDOW_PRIORITY_EXE);

	obs_source_t *source = obs_source_create_private(audioType, "MoonLit Chat", settings);
	if (source) {
		obs_source_set_audio_mixers(source, (1u << 3));
	}
	return source;
}

/* Track 1 (mixed): the desktop audio output device (what comes out of the
 * speakers/headphones), like the native OBS "Desktop Audio" source. */
obs_source_t *createMoonLitDesktopSource()
{
	static const char *const audioType = "wasapi_output_capture";
	if (!obs_get_latest_input_type_id(audioType))
		return nullptr;

	const char *deviceId = config_get_string(OBSBasic::Get()->Config(), "MoonLit", "DesktopDeviceId");
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "device_id", deviceId && *deviceId ? deviceId : "default");

	obs_source_t *source = obs_source_create_private(audioType, "MoonLit Audio de escritorio", settings);
	if (source) {
		obs_source_set_audio_mixers(source, (1u << 0));
	}
	return source;
}

/* Krisp-like noise suppression for the microphone: RNNoise at a strong
 * suppression level plus a fast noise gate, both native obs-filters. */
void applyMoonLitNoiseSuppression(obs_source_t *mic)
{
	if (!mic) {
		return;
	}

	const bool enabled = config_get_bool(OBSBasic::Get()->Config(), "MoonLit", "NoiseSuppression");
	const char *const suppressName = "MoonLit Supresion de ruido";
	const char *const gateName = "MoonLit Noise Gate";

	OBSSourceAutoRelease existingSuppress = obs_source_get_filter_by_name(mic, suppressName);
	OBSSourceAutoRelease existingGate = obs_source_get_filter_by_name(mic, gateName);

	if (enabled) {
		if (!existingSuppress) {
			OBSDataAutoRelease settings = obs_data_create();
			obs_data_set_string(settings, "method", "rnnoise");
			obs_data_set_int(settings, "suppress_level", -40);
			OBSSourceAutoRelease filter =
				obs_source_create_private("noise_suppress_filter", suppressName, settings);
			if (filter) {
				obs_source_filter_add(mic, filter);
			}
		}
		if (!existingGate) {
			OBSDataAutoRelease settings = obs_data_create();
			obs_data_set_int(settings, "open_threshold", -40);
			obs_data_set_int(settings, "close_threshold", -45);
			obs_data_set_int(settings, "attack_time", 10);
			obs_data_set_int(settings, "hold_time", 20);
			obs_data_set_int(settings, "release_time", 50);
			OBSSourceAutoRelease filter = obs_source_create_private("noise_gate_filter", gateName, settings);
			if (filter) {
				obs_source_filter_add(mic, filter);
			}
		}
	} else {
		if (existingSuppress) {
			obs_source_filter_remove(mic, existingSuppress);
		}
		if (existingGate) {
			obs_source_filter_remove(mic, existingGate);
		}
	}
}

void RemoveMoonLitAudioItems()
{
	if (moonlitAudioItem) {
		obs_sceneitem_remove(moonlitAudioItem);
		moonlitAudioItem = nullptr;
	}
	moonlitAudioSource = nullptr;
	if (moonlitMicItem) {
		obs_sceneitem_remove(moonlitMicItem);
		moonlitMicItem = nullptr;
	}
	moonlitMicSource = nullptr;
	if (moonlitChatItem) {
		obs_sceneitem_remove(moonlitChatItem);
		moonlitChatItem = nullptr;
	}
	moonlitChatSource = nullptr;
	if (moonlitDesktopItem) {
		obs_sceneitem_remove(moonlitDesktopItem);
		moonlitDesktopItem = nullptr;
	}
	moonlitDesktopSource = nullptr;
}

struct MoonLitMonitorSelection {
	HMONITOR target = nullptr;
	int index = 0;
	bool found = false;
	QString device;
};

BOOL CALLBACK selectMoonLitMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param)
{
	MoonLitMonitorSelection *selection = reinterpret_cast<MoonLitMonitorSelection *>(param);
	if (monitor != selection->target) {
		++selection->index;
		return TRUE;
	}

	MONITORINFOEXW info = {};
	info.cbSize = sizeof(info);
	selection->found = true;
	if (GetMonitorInfoW(monitor, reinterpret_cast<LPMONITORINFO>(&info)))
		selection->device = QString::fromWCharArray(info.szDevice);

	return FALSE;
}

void setMoonLitMonitorSettings(obs_data_t *settings, const MoonLitTarget &target)
{
	const HMONITOR monitor = MonitorFromWindow(reinterpret_cast<HWND>(target.window), MONITOR_DEFAULTTONEAREST);
	MoonLitMonitorSelection selection;
	selection.target = monitor;
	EnumDisplayMonitors(nullptr, nullptr, selectMoonLitMonitor, reinterpret_cast<LPARAM>(&selection));

	obs_data_set_int(settings, "method", 1); /* DXGI */
	obs_data_set_int(settings, "monitor", selection.found ? selection.index : 0);
	obs_data_set_bool(settings, "capture_cursor", false);
	obs_data_set_bool(settings, "force_sdr", false);
	if (!selection.device.isEmpty())
		obs_data_set_string(settings, "monitor_id", selection.device.toUtf8().constData());
}

bool readMoonLitCaptureHealth(obs_source_t *source, bool &active, bool &firstFrame, bool &wgc)
{
	if (!source)
		return false;

	proc_handler_t *ph = obs_source_get_proc_handler(source);
	calldata_t data = {0};
	const bool called = ph && proc_handler_call(ph, "get_capture_health", &data);
	if (called) {
		active = calldata_bool(&data, "active");
		firstFrame = calldata_bool(&data, "first_frame");
		wgc = calldata_bool(&data, "wgc");
	}
	calldata_free(&data);
	return called;
}
#endif

} // namespace

void OBSBasic::ShowMoonLitLibrary()
{
	if (!moonlitDashboard || !moonlitLibrary) {
		return;
	}
	moonlitDashboard->hide();
	moonlitLibrary->refresh();
	moonlitLibrary->show();
}

void OBSBasic::ShowMoonLitDashboard()
{
	if (moonlitLibrary) {
		moonlitLibrary->hide();
	}
	if (moonlitDashboard) {
		moonlitDashboard->show();
	}
}

void OBSBasic::InitializeMoonLitShell()
{
	if (!moonlitDashboard) {
		setWindowTitle(QStringLiteral("MoonLit"));
		setObjectName(QStringLiteral("MoonLitMainWindow"));

#ifdef MOONLIT_BUILD
		/* Medal-style clip hotkey: works while a game has focus through the
		 * low-level obs hotkey hook (windowed and borderless games). */
		const obs_hotkey_id saveClipHotkey = obs_hotkey_register_frontend(
			"MoonLit.SaveClip", "Guardar clip",
			[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
				if (!pressed) {
					return;
				}
				auto *main = static_cast<OBSBasic *>(data);
				QMetaObject::invokeMethod(main, [main]() { main->ReplayBufferSave(); },
							  Qt::QueuedConnection);
			},
			this);
		if (saveClipHotkey != OBS_INVALID_HOTKEY_ID) {
			obs_key_combination_t defaultCombo = {0, OBS_KEY_F8};
			obs_hotkey_load_bindings(saveClipHotkey, &defaultCombo, 1);
		}
#endif

		moonlitDashboard = new MoonLitDashboard(this);
		ui->previewLayout->addWidget(moonlitDashboard);
		moonlitLibrary = new MoonLitLibraryWidget(this);
		ui->previewLayout->addWidget(moonlitLibrary);
		moonlitLibrary->hide();

		connect(moonlitDashboard, &MoonLitDashboard::replayActionRequested, this,
			[this]() { ReplayBufferActionTriggered(); });
		connect(moonlitDashboard, &MoonLitDashboard::saveClipRequested, this, &OBSBasic::ReplayBufferSave);
		connect(moonlitDashboard, &MoonLitDashboard::settingsRequested, this, [this]() {
			MoonLitSettingsDialog dialog(this);
			dialog.exec();
		});
		connect(moonlitDashboard, &MoonLitDashboard::libraryRequested, this, &OBSBasic::ShowMoonLitLibrary);
		connect(moonlitLibrary, &MoonLitLibraryWidget::backRequested, this, &OBSBasic::ShowMoonLitDashboard);
		connect(moonlitLibrary, &MoonLitLibraryWidget::libraryUpdated, moonlitDashboard,
			[this](const QVector<MoonLit::Clip> &clips) {
				QVector<MoonLit::Clip> recent = clips;
				std::sort(recent.begin(), recent.end(),
					  [](const MoonLit::Clip &left, const MoonLit::Clip &right) {
						  return left.createdAtUtc > right.createdAtUtc;
					  });
				moonlitDashboard->setRecentClips(recent);
			});
		connect(moonlitDashboard, &MoonLitDashboard::recentClipRequested, this,
			[this](const QString &id) {
				ShowMoonLitLibrary();
				moonlitLibrary->selectClip(id);
			});
		connect(this, &OBSBasic::ReplayClipSaved, moonlitLibrary, &MoonLitLibraryWidget::ingestClip);
		connect(this, &OBSBasic::ReplayClipSaved, moonlitDashboard,
			[this](const QString &path) { moonlitDashboard->setClipSaved(path); });
		connect(this, &OBSBasic::ReplaySaveFailed, moonlitDashboard,
			[this](int code) {
				QString message;
				if (code == OBS_OUTPUT_UNSUPPORTED)
					message = QStringLiteral("el guardado no es compatible con el encoder");
				else if (code == OBS_OUTPUT_NO_SPACE)
					message = QStringLiteral("no hay espacio en disco");
				else
					message = QStringLiteral("fallo al guardar el clip");
				moonlitDashboard->setClipError(message);
			});

		connect(this, &OBSBasic::ReplayBufStarted, this,
			[this]() {
				moonlitReplayStartFailures = 0;
				moonlitReplayAutoBlocked = false;
				moonlitReplayManualStopRequested = false;
				moonlitReplayStartRequested = true;
				moonlitDashboard->setReplayState(true);
			});
		connect(this, &OBSBasic::ReplayBufStopping, this,
			[this]() {
				moonlitReplayManualStopRequested = true;
				moonlitDashboard->setReplayState(true, true);
			});
		connect(this, &OBSBasic::ReplayBufStopped, this,
			[this]() {
				moonlitReplayStartRequested = false;
				if (moonlitReplayManualStopRequested)
					moonlitReplayAutoBlocked = true;
				moonlitReplayManualStopRequested = false;
				moonlitDashboard->setReplayState(false);
			});
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

	moonlitTargetFocused = false;
	moonlitUsingMonitorFallback = false;
	moonlitReplayStartRequested = false;
	moonlitReplayStartFailures = 0;
	moonlitReplayAutoBlocked = false;
	moonlitReplayManualStopRequested = false;
	moonlitDetector = new MoonLitGameDetector(this);
	connect(moonlitDetector, &MoonLitGameDetector::targetDetected, this,
		[this](const MoonLitTarget &target) { ConfigureMoonLitCapture(target); });
	connect(moonlitDetector, &MoonLitGameDetector::targetFocusChanged, this, [this](bool focused) {
		moonlitTargetFocused = focused;
		setMoonLitCaptureCovered(moonlitCaptureItem);
		if (moonlitAudioSource) {
			obs_source_set_enabled(moonlitAudioSource, focused);
		}
		if (focused && !moonlitUsingMonitorFallback) {
			moonlitWgcTimer.restart();
		}
		if (moonlitDashboard) {
			moonlitDashboard->setCaptureStatus(
				focused ? (moonlitUsingMonitorFallback ? QStringLiteral("DXGI monitor fallback")
										: QStringLiteral("captura de ventana inicializando"))
					: QStringLiteral("pausada temporalmente (Alt+Tab)"));
		}
	});
	connect(moonlitDetector, &MoonLitGameDetector::targetLost, this, [this]() {
		moonlitTargetFocused = false;
		if (moonlitDashboard) {
			moonlitDashboard->setDetectedGame(QString());
			moonlitDashboard->setCaptureStatus(QStringLiteral("juego cerrado"));
		}
		/* Hide the capture before requesting the asynchronous output stop. */
		ShieldMoonLitCapture();
		if (ReplayBufferActive()) {
			StopReplayBuffer();
		}
		ClearMoonLitCapture();
	});

	QTimer *captureHealthTimer = new QTimer(this);
	captureHealthTimer->setInterval(250);
	connect(captureHealthTimer, &QTimer::timeout, this, [this]() {
		if (isClosing() || !moonlitTargetFocused)
			return;

		if (!moonlitCaptureSource) {
			if (moonlitTarget.isValid() &&
			    (!moonlitConfigureRetryTimer.isValid() || moonlitConfigureRetryTimer.elapsed() >= 1000)) {
				moonlitConfigureRetryTimer.restart();
				ConfigureMoonLitCapture(moonlitTarget);
			}
			return;
		}

		if (moonlitUsingMonitorFallback) {
			const bool ready = obs_source_get_width(moonlitCaptureSource) > 0 &&
					   obs_source_get_height(moonlitCaptureSource) > 0;
			if (ready) {
				setMoonLitCaptureShielded(moonlitCaptureItem, false);
				if (moonlitDashboard)
					moonlitDashboard->setCaptureStatus(QStringLiteral("DXGI monitor fallback"));
				if (!ReplayBufferActive() && !moonlitReplayStartRequested && !moonlitReplayAutoBlocked &&
				    (!moonlitReplayRetryTimer.isValid() || moonlitReplayRetryTimer.elapsed() >= 1000)) {
					moonlitReplayRetryTimer.restart();
					moonlitReplayStartRequested = true;
					StartReplayBuffer();
					if (!ReplayBufferActive()) {
						moonlitReplayStartRequested = false;
						moonlitReplayAutoBlocked = ++moonlitReplayStartFailures >= 3;
					}
				}
			}
			return;
		}

		bool active = false;
		bool firstFrame = false;
		bool wgc = false;
		const bool healthAvailable = readMoonLitCaptureHealth(moonlitCaptureSource, active, firstFrame, wgc);
		if (healthAvailable && active && firstFrame && wgc) {
			setMoonLitCaptureShielded(moonlitCaptureItem, false);
			if (moonlitDashboard)
				moonlitDashboard->setCaptureStatus(QStringLiteral("WGC de ventana"));
			if (!ReplayBufferActive() && !moonlitReplayStartRequested && !moonlitReplayAutoBlocked &&
			    (!moonlitReplayRetryTimer.isValid() || moonlitReplayRetryTimer.elapsed() >= 1000)) {
				moonlitReplayRetryTimer.restart();
				moonlitReplayStartRequested = true;
				StartReplayBuffer();
				if (!ReplayBufferActive()) {
					moonlitReplayStartRequested = false;
					moonlitReplayAutoBlocked = ++moonlitReplayStartFailures >= 3;
				}
			} else if (ReplayBufferActive()) {
				moonlitReplayStartFailures = 0;
				moonlitReplayAutoBlocked = false;
				moonlitReplayStartRequested = true;
			}
			return;
		}

		if (moonlitDashboard)
			moonlitDashboard->setCaptureStatus(QStringLiteral("captura de ventana inicializando"));

		if (!moonlitWgcTimer.isValid() || moonlitWgcTimer.elapsed() < 5000)
			return;

		setMoonLitCaptureCovered(moonlitCaptureItem);

		if (!monitorFallbackIsSafe(moonlitTarget)) {
			if (moonlitDashboard)
				moonlitDashboard->setCaptureStatus(QStringLiteral("fallback bloqueado: ventana no cubre el monitor"));
			moonlitWgcTimer.restart();
			return;
		}

		OBSScene scene = GetCurrentScene();
		if (!scene)
			return;

		if (moonlitCaptureItem) {
			obs_sceneitem_remove(moonlitCaptureItem);
			moonlitCaptureItem = nullptr;
		}
		moonlitCaptureSource = nullptr;

		OBSDataAutoRelease settings = obs_data_create();
		setMoonLitMonitorSettings(settings, moonlitTarget);
		OBSSourceAutoRelease monitor =
			obs_source_create_private("monitor_capture", "MoonLit Monitor Fallback", settings);
		if (!monitor) {
			blog(LOG_WARNING, "MoonLit: safe monitor fallback is unavailable");
			moonlitWgcTimer.restart();
			if (moonlitDashboard)
				moonlitDashboard->setCaptureStatus(QStringLiteral("captura segura no disponible"));
			return;
		}

		moonlitCaptureSource = monitor;
		obs_source_set_enabled(monitor, false);
		moonlitCaptureItem = obs_scene_add(scene, monitor);
		if (!moonlitCaptureItem) {
			blog(LOG_WARNING, "MoonLit: unable to add the safe monitor fallback");
			moonlitCaptureSource = nullptr;
			moonlitWgcTimer.restart();
			return;
		}

		setMoonLitBounds(moonlitCaptureItem);
		obs_sceneitem_set_order(moonlitShieldItem, OBS_ORDER_MOVE_TOP);
		obs_source_set_enabled(monitor, true);
		setMoonLitCaptureCovered(moonlitCaptureItem);
		moonlitUsingMonitorFallback = true;
		if (moonlitDashboard)
			moonlitDashboard->setCaptureStatus(QStringLiteral("fallback de monitor inicializando"));
	});
	captureHealthTimer->start();
	moonlitDetector->start();
}

void OBSBasic::ConfigureMoonLitCapture(const MoonLitTarget &target)
{
	if (!target.isValid()) {
		return;
	}

	ClearMoonLitCapture();
	moonlitTarget = target;
	moonlitTargetFocused = true;
	moonlitUsingMonitorFallback = false;
	moonlitReplayStartRequested = false;
	moonlitReplayStartFailures = 0;
	moonlitReplayAutoBlocked = false;
	moonlitWgcTimer.restart();
	moonlitConfigureRetryTimer.restart();

	OBSScene scene = GetCurrentScene();
	if (!scene) {
		blog(LOG_ERROR, "MoonLit: no active scene for automatic capture");
		return;
	}

	OBSDataAutoRelease settings = obs_data_create();
	const std::string selector = makeWindowSelector(target);
	obs_data_set_string(settings, "window", selector.c_str());
	obs_data_set_int(settings, "method", 2); /* Windows Graphics Capture */
	obs_data_set_int(settings, "priority", WINDOW_PRIORITY_EXE);
	obs_data_set_bool(settings, "cursor", false);
	obs_data_set_bool(settings, "compatibility", false);
	obs_data_set_bool(settings, "client_area", true);
	obs_data_set_bool(settings, "force_sdr", false);
	obs_data_set_bool(settings, "capture_audio", false);
	obs_data_set_bool(settings, "moonlit_require_wgc", true);
	obs_data_set_int(settings, "moonlit_hwnd", static_cast<int64_t>(target.window));
	obs_data_set_int(settings, "moonlit_process_id", target.processId);
	obs_data_set_int(settings, "moonlit_creation_time", static_cast<int64_t>(target.creationTime));

	moonlitCaptureSource = obs_source_create_private("window_capture", "MoonLit Game", settings);
	if (!moonlitCaptureSource) {
		blog(LOG_ERROR, "MoonLit: unable to create the window capture source");
		if (moonlitDashboard)
			moonlitDashboard->setCaptureStatus(QStringLiteral("captura de ventana no disponible"));
		return;
	}

	if (obs_get_latest_input_type_id("wasapi_process_output_capture")) {
		OBSSourceAutoRelease audio = createMoonLitAudioSource(selector);
		if (audio) {
			moonlitAudioSource = audio;
			moonlitAudioItem = obs_scene_add(scene, audio);
			if (!moonlitAudioItem)
				moonlitAudioSource = nullptr;
		} else {
			blog(LOG_WARNING, "MoonLit: process audio source could not be created");
		}
	}

	if (obs_get_latest_input_type_id("wasapi_input_capture")) {
		OBSSourceAutoRelease mic = createMoonLitMicSource();
		if (mic) {
			moonlitMicSource = mic;
			moonlitMicItem = obs_scene_add(scene, mic);
			if (!moonlitMicItem)
				moonlitMicSource = nullptr;
			applyMoonLitNoiseSuppression(moonlitMicSource);
		}
	}

	OBSSourceAutoRelease chat = createMoonLitChatSource();
	if (chat) {
		moonlitChatSource = chat;
		moonlitChatItem = obs_scene_add(scene, chat);
		if (!moonlitChatItem)
			moonlitChatSource = nullptr;
	}

	OBSSourceAutoRelease desktop = createMoonLitDesktopSource();
	if (desktop) {
		moonlitDesktopSource = desktop;
		moonlitDesktopItem = obs_scene_add(scene, desktop);
		if (!moonlitDesktopItem)
			moonlitDesktopSource = nullptr;
	}

	createMoonLitShield(scene);
	if (!moonlitShieldItem) {
		blog(LOG_ERROR, "MoonLit: capture shield could not be installed");
		moonlitCaptureSource = nullptr;
		if (moonlitAudioItem) {
			obs_sceneitem_remove(moonlitAudioItem);
			moonlitAudioItem = nullptr;
		}
		moonlitAudioSource = nullptr;
		RemoveMoonLitAudioItems();
		return;
	}

	obs_source_set_enabled(moonlitCaptureSource, false);
	moonlitCaptureItem = obs_scene_add(scene, moonlitCaptureSource);
	if (!moonlitCaptureItem) {
		blog(LOG_ERROR, "MoonLit: unable to add the window capture source to the active scene");
		if (moonlitShieldItem) {
			obs_sceneitem_remove(moonlitShieldItem);
			moonlitShieldItem = nullptr;
		}
		moonlitCaptureSource = nullptr;
		RemoveMoonLitAudioItems();
		return;
	}
	setMoonLitBounds(moonlitCaptureItem);
	obs_sceneitem_set_order(moonlitShieldItem, OBS_ORDER_MOVE_TOP);
	obs_source_set_enabled(moonlitCaptureSource, true);
	setMoonLitCaptureCovered(moonlitCaptureItem);

	if (moonlitDashboard) {
		moonlitDashboard->setDetectedGame(target.executable);
		moonlitDashboard->setCaptureStatus(QStringLiteral("captura de ventana inicializando"));
	}
	UpdateMoonLitMixer();
}

void OBSBasic::UpdateMoonLitMixer()
{
	if (!moonlitDashboard)
		return;

	MoonLitMixer *mixer = moonlitDashboard->mixer();
	if (!mixer)
		return;

	mixer->clearSources();
	mixer->addSource(QStringLiteral("Escritorio"), moonlitDesktopSource);
	mixer->addSource(QStringLiteral("Juego"), moonlitAudioSource);
	mixer->addSource(QStringLiteral("Microfono"), moonlitMicSource);
	mixer->addSource(QStringLiteral("Chat"), moonlitChatSource);
}

void OBSBasic::ApplyMoonLitNoiseSuppression()
{
	applyMoonLitNoiseSuppression(moonlitMicSource);
}

void OBSBasic::ShieldMoonLitCapture()
{
	setMoonLitCaptureShielded(moonlitCaptureItem, true);
	if (moonlitAudioSource)
		obs_source_set_enabled(moonlitAudioSource, false);
}

void OBSBasic::ClearMoonLitCapture()
{
	ShieldMoonLitCapture();
	if (moonlitCaptureItem) {
		obs_sceneitem_remove(moonlitCaptureItem);
		moonlitCaptureItem = nullptr;
	}
	moonlitCaptureSource = nullptr;
	RemoveMoonLitAudioItems();
	if (moonlitShieldItem) {
		obs_sceneitem_remove(moonlitShieldItem);
		moonlitShieldItem = nullptr;
	}
	moonlitTarget = {};
	moonlitUsingMonitorFallback = false;
	moonlitReplayStartRequested = false;
	moonlitReplayStartFailures = 0;
	moonlitReplayAutoBlocked = false;
	moonlitReplayManualStopRequested = false;
	moonlitWgcTimer.invalidate();
	moonlitReplayRetryTimer.invalidate();
	moonlitConfigureRetryTimer.invalidate();
	if (moonlitDashboard) {
		moonlitDashboard->mixer()->clearSources();
	}
}
#endif
