#include "HotkeyManager.hpp"

#include <obs.h>
#include <obs-hotkey.h>

#include <QMetaObject>

namespace MoonLit {

HotkeyManager::HotkeyManager(QObject *parent) : QObject(parent) {}

void HotkeyManager::registerSaveClip(config_t *config, const std::function<void()> &action)
{
	action_ = action;
	if (saveClipId_ != 0) {
		obs_hotkey_unregister(saveClipId_);
		saveClipId_ = 0;
	}

	/* F8 saves the clip while a game has focus; the callback is queued to
	 * the UI thread because the hotkey fires from the engine thread. */
	const obs_hotkey_id id = obs_hotkey_register_frontend(
		"MoonLit.SaveClip", "Guardar clip",
		[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
			if (!pressed) {
				return;
			}
			auto *self = static_cast<HotkeyManager *>(data);
			QMetaObject::invokeMethod(self, [self]() {
				if (self->action_) {
					self->action_();
				}
			}, Qt::QueuedConnection);
		},
		this);
	if (id != OBS_INVALID_HOTKEY_ID) {
		const char *info = config ? config_get_string(config, "Hotkeys", "MoonLit.SaveClip") : nullptr;
		if (info) {
			OBSDataAutoRelease data = obs_data_create_from_json(info);
			OBSDataArrayAutoRelease array = obs_data_get_array(data, "bindings");
			obs_hotkey_load(id, array);
		} else {
			obs_key_combination_t defaultCombo = {0, OBS_KEY_F8};
			obs_hotkey_load_bindings(id, &defaultCombo, 1);
		}
		saveClipId_ = static_cast<uint32_t>(id);
	}
}

void HotkeyManager::setSaveClipHotkey(config_t *config, obs_key_combination_t combo)
{
	if (saveClipId_ == 0) {
		return;
	}

	/* Apply in the engine: same path OBSHotkeyWidget::Save uses so the
	 * hotkey_bindings_changed signal keeps the rest of the UI in sync. */
	if (obs_key_combination_is_empty(combo)) {
		obs_hotkey_load_bindings(saveClipId_, nullptr, 0);
	} else {
		obs_hotkey_load_bindings(saveClipId_, &combo, 1);
	}

	/* Persist in the same {"bindings": [...]} format the stock settings
	 * dialog writes, so a manual re-bind survives restarts. */
	OBSDataArrayAutoRelease array = obs_hotkey_save(saveClipId_);
	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_array(data, "bindings", array);
	config_set_string(config, "Hotkeys", "MoonLit.SaveClip", obs_data_get_json(data));
	config_save_safe(config, "tmp", nullptr);
}

obs_key_combination_t HotkeyManager::saveClipHotkey() const
{
	obs_key_combination_t result = {0, OBS_KEY_NONE};
	if (saveClipId_ == 0) {
		return result;
	}

	struct Context {
		const HotkeyManager *self;
		obs_key_combination_t *result;
	} context{this, &result};

	obs_enum_hotkey_bindings(
		[](void *data, size_t, obs_hotkey_binding_t *binding) {
			auto *context = static_cast<Context *>(data);
			if (obs_hotkey_binding_get_hotkey_id(binding) != context->self->saveClipId_) {
				return true;
			}
			*context->result = obs_hotkey_binding_get_key_combination(binding);
			return false;
		},
		&context);

	return result;
}

} // namespace MoonLit
