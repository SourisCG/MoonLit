#include "WindowsCaptureBackend.hpp"

#include <obs.h>

#include <util/windows/window-helpers.h>

#include <QString>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MoonLit {

namespace {

std::string encodeWindowPart(QString value)
{
	value.replace(QStringLiteral("#"), QStringLiteral("#22"));
	value.replace(QStringLiteral(":"), QStringLiteral("#3A"));
	return value.toUtf8().toStdString();
}

std::string makeWindowSelector(const CaptureTarget &target)
{
	QString title = QString::fromStdString(target.name);
	QString windowClass = QString::fromStdString(target.windowClass);
	QString executable = QString::fromStdString(target.executablePath);
	return encodeWindowPart(title) + ":" + encodeWindowPart(windowClass) + ":" +
	       encodeWindowPart(executable);
}

uintptr_t windowHandle(const CaptureTarget &target)
{
	if (const auto *handle = std::get_if<uintptr_t>(&target.window)) {
		return *handle;
	}
	return 0;
}

struct MonitorSelection {
	HMONITOR target = nullptr;
	int index = 0;
	bool found = false;
	QString device;
};

BOOL CALLBACK selectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param)
{
	MonitorSelection *selection = reinterpret_cast<MonitorSelection *>(param);
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

} // namespace

WindowsCaptureBackend::WindowsCaptureBackend(ICaptureHost *host) : host_(host) {}

bool WindowsCaptureBackend::attachWindow(const CaptureTarget &target)
{
	if (!target.isValid()) {
		return false;
	}

	detach();

	OBSScene scene = host_ ? host_->moonlitCurrentScene() : nullptr;
	if (!scene) {
		blog(LOG_ERROR, "MoonLit: no active scene for automatic capture");
		return false;
	}

	const std::string selector = makeWindowSelector(target);
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "window", selector.c_str());
	obs_data_set_int(settings, "method", 2); /* Windows Graphics Capture */
	obs_data_set_int(settings, "priority", WINDOW_PRIORITY_EXE);
	obs_data_set_bool(settings, "cursor", false);
	obs_data_set_bool(settings, "compatibility", false);
	obs_data_set_bool(settings, "client_area", true);
	obs_data_set_bool(settings, "force_sdr", false);
	obs_data_set_bool(settings, "capture_audio", false);
	obs_data_set_bool(settings, "moonlit_require_wgc", true);
	obs_data_set_int(settings, "moonlit_hwnd", static_cast<int64_t>(windowHandle(target)));
	obs_data_set_int(settings, "moonlit_process_id", static_cast<int64_t>(target.processId));
	obs_data_set_int(settings, "moonlit_creation_time", static_cast<int64_t>(target.creationTimeNs));

	captureSource_ = obs_source_create_private("window_capture", "MoonLit Game", settings);
	if (!captureSource_) {
		blog(LOG_ERROR, "MoonLit: unable to create the window capture source");
		return false;
	}

	audioSource_ = createGameAudioSource(selector, target);
	micSource_ = createMicSource();
	chatSource_ = createChatSource();
	desktopSource_ = createDesktopSource();
	applyNoiseSuppression();

	if (!installShield()) {
		blog(LOG_ERROR, "MoonLit: capture shield could not be installed");
		detach();
		return false;
	}

	obs_source_set_enabled(captureSource_, false);
	captureItem_ = obs_scene_add(scene, captureSource_);
	if (!captureItem_) {
		blog(LOG_ERROR, "MoonLit: unable to add the window capture source to the active scene");
		detach();
		return false;
	}
	setBounds(captureItem_);
	if (shieldItem_) {
		obs_sceneitem_set_order(shieldItem_, OBS_ORDER_MOVE_TOP);
	}
	obs_source_set_enabled(captureSource_, true);
	cover();
	return true;
}

bool WindowsCaptureBackend::attachMonitorFallback(const CaptureTarget &target)
{
	OBSScene scene = host_ ? host_->moonlitCurrentScene() : nullptr;
	if (!scene) {
		return false;
	}

	OBSDataAutoRelease settings = obs_data_create();
	const HMONITOR monitor =
		MonitorFromWindow(reinterpret_cast<HWND>(windowHandle(target)), MONITOR_DEFAULTTONEAREST);
	MonitorSelection selection;
	selection.target = monitor;
	EnumDisplayMonitors(nullptr, nullptr, selectMonitor, reinterpret_cast<LPARAM>(&selection));

	obs_data_set_int(settings, "method", 1); /* DXGI */
	obs_data_set_int(settings, "monitor", selection.found ? selection.index : 0);
	obs_data_set_bool(settings, "capture_cursor", false);
	obs_data_set_bool(settings, "force_sdr", false);
	if (!selection.device.isEmpty())
		obs_data_set_string(settings, "monitor_id", selection.device.toUtf8().constData());

	OBSSourceAutoRelease monitorSource =
		obs_source_create_private("monitor_capture", "MoonLit Monitor Fallback", settings);
	if (!monitorSource) {
		blog(LOG_WARNING, "MoonLit: safe monitor fallback is unavailable");
		return false;
	}

	captureSource_ = monitorSource;
	obs_source_set_enabled(captureSource_, false);
	captureItem_ = obs_scene_add(scene, captureSource_);
	if (!captureItem_) {
		blog(LOG_WARNING, "MoonLit: unable to add the safe monitor fallback");
		captureSource_ = nullptr;
		return false;
	}

	setBounds(captureItem_);
	if (shieldItem_) {
		obs_sceneitem_set_order(shieldItem_, OBS_ORDER_MOVE_TOP);
	}
	obs_source_set_enabled(captureSource_, true);
	return true;
}

bool WindowsCaptureBackend::hasVideo() const
{
	return captureSource_ && obs_source_get_width(captureSource_) > 0 &&
	       obs_source_get_height(captureSource_) > 0;
}

CaptureHealth WindowsCaptureBackend::health() const
{
	CaptureHealth result;
	if (!captureSource_) {
		return result;
	}

	proc_handler_t *ph = obs_source_get_proc_handler(captureSource_);
	calldata_t data = {0};
	const bool called = ph && proc_handler_call(ph, "get_capture_health", &data);
	if (called) {
		result.active = calldata_bool(&data, "active");
		result.firstFrameReceived = calldata_bool(&data, "first_frame");
		result.activeKind = calldata_bool(&data, "wgc") ? BackendKind::Wgc : BackendKind::DxgiMonitor;
	}
	calldata_free(&data);
	return result;
}

void WindowsCaptureBackend::shield()
{
	if (captureItem_) {
		obs_sceneitem_set_visible(captureItem_, false);
	}
	if (shieldItem_) {
		obs_sceneitem_set_visible(shieldItem_, true);
	}
}

void WindowsCaptureBackend::cover()
{
	if (shieldItem_) {
		obs_sceneitem_set_visible(shieldItem_, true);
		if (captureItem_) {
			obs_sceneitem_set_visible(captureItem_, true);
		}
	} else if (captureItem_) {
		obs_sceneitem_set_visible(captureItem_, false);
	}
}

void WindowsCaptureBackend::reveal()
{
	if (!shieldItem_) {
		if (captureItem_) {
			obs_sceneitem_set_visible(captureItem_, false);
		}
		return;
	}

	/* Reveal only after the shield is already covering the source. */
	obs_sceneitem_set_visible(shieldItem_, true);
	if (captureItem_) {
		obs_sceneitem_set_visible(captureItem_, true);
	}
	obs_sceneitem_set_visible(shieldItem_, false);
}

void WindowsCaptureBackend::detach()
{
	shield();
	if (captureItem_) {
		obs_sceneitem_remove(captureItem_);
		captureItem_ = nullptr;
	}
	captureSource_ = nullptr;
	removeAudioItems();
	if (shieldItem_) {
		obs_sceneitem_remove(shieldItem_);
		shieldItem_ = nullptr;
	}
}

void WindowsCaptureBackend::setProcessAudioEnabled(bool enabled)
{
	if (audioSource_) {
		obs_source_set_enabled(audioSource_, enabled);
	}
}

void WindowsCaptureBackend::applyNoiseSuppression()
{
	if (!micSource_) {
		return;
	}

	const bool enabled = host_ && config_get_bool(host_->activeConfig(), "MoonLit", "NoiseSuppression");
	const char *const suppressName = "MoonLit Supresion de ruido";
	const char *const gateName = "MoonLit Noise Gate";

	OBSSourceAutoRelease existingSuppress = obs_source_get_filter_by_name(micSource_, suppressName);
	OBSSourceAutoRelease existingGate = obs_source_get_filter_by_name(micSource_, gateName);

	if (enabled) {
		if (!existingSuppress) {
			OBSDataAutoRelease settings = obs_data_create();
			obs_data_set_string(settings, "method", "rnnoise");
			obs_data_set_int(settings, "suppress_level", -40);
			OBSSourceAutoRelease filter =
				obs_source_create_private("noise_suppress_filter", suppressName, settings);
			if (filter) {
				obs_source_filter_add(micSource_, filter);
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
				obs_source_filter_add(micSource_, filter);
			}
		}
	} else {
		if (existingSuppress) {
			obs_source_filter_remove(micSource_, existingSuppress);
		}
		if (existingGate) {
			obs_source_filter_remove(micSource_, existingGate);
		}
	}
}

bool WindowsCaptureBackend::monitorFallbackIsSafe(const CaptureTarget &target) const
{
	const HWND window = reinterpret_cast<HWND>(windowHandle(target));
	if (!IsWindow(window) || IsIconic(window) || !IsWindowVisible(window))
		return false;

	const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
	MONITORINFO info = {};
	info.cbSize = sizeof(info);
	RECT windowRect = {};
	return monitor && GetMonitorInfoW(monitor, &info) && GetWindowRect(window, &windowRect) &&
	       EqualRect(&info.rcMonitor, &windowRect);
}

bool WindowsCaptureBackend::installShield()
{
	OBSScene scene = host_ ? host_->moonlitCurrentScene() : nullptr;
	if (!scene) {
		return false;
	}

	struct obs_video_info videoInfo = {};
	if (!obs_get_video_info(&videoInfo)) {
		return false;
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_int(settings, "color", 0xFF000000);
	obs_data_set_int(settings, "width", videoInfo.base_width);
	obs_data_set_int(settings, "height", videoInfo.base_height);

	OBSSourceAutoRelease shield = obs_source_create_private("color_source", "MoonLit Capture Shield", settings);
	if (!shield) {
		return false;
	}

	shieldItem_ = obs_scene_add(scene, shield);
	if (!shieldItem_) {
		return false;
	}
	setBounds(shieldItem_);
	obs_sceneitem_set_visible(shieldItem_, true);
	obs_sceneitem_set_locked(shieldItem_, true);
	return true;
}

void WindowsCaptureBackend::setBounds(obs_sceneitem_t *item)
{
	if (!item) {
		return;
	}

	struct obs_video_info videoInfo = {};
	if (!obs_get_video_info(&videoInfo)) {
		return;
	}

	vec2 bounds;
	vec2_set(&bounds, static_cast<float>(videoInfo.base_width), static_cast<float>(videoInfo.base_height));
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);
	obs_sceneitem_set_bounds(item, &bounds);
}

OBSSource WindowsCaptureBackend::createGameAudioSource(const std::string &selector, const CaptureTarget &target)
{
	static const char *const audioType = "wasapi_process_output_capture";
	if (!obs_get_latest_input_type_id(audioType)) {
		return nullptr;
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "window", selector.c_str());
	obs_data_set_int(settings, "priority", WINDOW_PRIORITY_EXE);
	obs_data_set_int(settings, "moonlit_hwnd", static_cast<int64_t>(windowHandle(target)));
	obs_data_set_int(settings, "moonlit_process_id", static_cast<int64_t>(target.processId));
	obs_data_set_int(settings, "moonlit_creation_time", static_cast<int64_t>(target.creationTimeNs));
	OBSSourceAutoRelease source =
		obs_source_create_private(audioType, "MoonLit Game Audio", settings);
	if (!source) {
		return nullptr;
	}

	/* Track 2: game only. */
	obs_source_set_audio_mixers(source, (1u << 1));
	OBSScene scene = host_ ? host_->moonlitCurrentScene() : nullptr;
	if (scene) {
		audioItem_ = obs_scene_add(scene, source);
		if (!audioItem_) {
			return nullptr;
		}
	}
	return OBSSource(source);
}

/* Track 3: explicit microphone with persisted device. */
OBSSource WindowsCaptureBackend::createMicSource()
{
	static const char *const audioType = "wasapi_input_capture";
	if (!obs_get_latest_input_type_id(audioType)) {
		return nullptr;
	}

	const char *deviceId = host_ ? config_get_string(host_->activeConfig(), "MoonLit", "MicDeviceId") : nullptr;
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "device_id", deviceId && *deviceId ? deviceId : "default");

	OBSSourceAutoRelease source = obs_source_create_private(audioType, "MoonLit Micrófono", settings);
	if (!source) {
		return nullptr;
	}
	obs_source_set_audio_mixers(source, (1u << 2));
	OBSScene scene = host_ ? host_->moonlitCurrentScene() : nullptr;
	if (scene) {
		micItem_ = obs_scene_add(scene, source);
		if (!micItem_) {
			return nullptr;
		}
	}
	return OBSSource(source);
}

/* Track 4: chat process audio (e.g. Discord) with exe-based restart recovery. */
OBSSource WindowsCaptureBackend::createChatSource()
{
	const char *chatExe = host_ ? config_get_string(host_->activeConfig(), "MoonLit", "ChatExe") : nullptr;
	if (!chatExe || !*chatExe) {
		return nullptr;
	}

	static const char *const audioType = "wasapi_process_output_capture";
	if (!obs_get_latest_input_type_id(audioType)) {
		return nullptr;
	}

	std::string selector = "::" + encodeWindowPart(QString::fromUtf8(chatExe));
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "window", selector.c_str());
	obs_data_set_int(settings, "priority", WINDOW_PRIORITY_EXE);

	OBSSourceAutoRelease source = obs_source_create_private(audioType, "MoonLit Chat", settings);
	if (!source) {
		return nullptr;
	}
	obs_source_set_audio_mixers(source, (1u << 3));
	OBSScene scene = host_ ? host_->moonlitCurrentScene() : nullptr;
	if (scene) {
		chatItem_ = obs_scene_add(scene, source);
		if (!chatItem_) {
			return nullptr;
		}
	}
	return OBSSource(source);
}

/* Track 1 (mixed): the desktop audio output device. */
OBSSource WindowsCaptureBackend::createDesktopSource()
{
	static const char *const audioType = "wasapi_output_capture";
	if (!obs_get_latest_input_type_id(audioType)) {
		return nullptr;
	}

	const char *deviceId = host_ ? config_get_string(host_->activeConfig(), "MoonLit", "DesktopDeviceId") : nullptr;
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "device_id", deviceId && *deviceId ? deviceId : "default");

	OBSSourceAutoRelease source = obs_source_create_private(audioType, "MoonLit Audio de escritorio", settings);
	if (!source) {
		return nullptr;
	}
	obs_source_set_audio_mixers(source, (1u << 0));
	OBSScene scene = host_ ? host_->moonlitCurrentScene() : nullptr;
	if (scene) {
		desktopItem_ = obs_scene_add(scene, source);
		if (!desktopItem_) {
			return nullptr;
		}
	}
	return OBSSource(source);
}

void WindowsCaptureBackend::removeAudioItems()
{
	if (audioItem_) {
		obs_sceneitem_remove(audioItem_);
		audioItem_ = nullptr;
	}
	audioSource_ = nullptr;
	if (micItem_) {
		obs_sceneitem_remove(micItem_);
		micItem_ = nullptr;
	}
	micSource_ = nullptr;
	if (chatItem_) {
		obs_sceneitem_remove(chatItem_);
		chatItem_ = nullptr;
	}
	chatSource_ = nullptr;
	if (desktopItem_) {
		obs_sceneitem_remove(desktopItem_);
		desktopItem_ = nullptr;
	}
	desktopSource_ = nullptr;
}

} // namespace MoonLit
