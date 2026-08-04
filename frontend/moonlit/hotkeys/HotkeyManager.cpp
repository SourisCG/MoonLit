#include "HotkeyManager.hpp"

#include <obs.h>
#include <obs-hotkey.h>

#include <QMetaObject>

namespace MoonLit {

HotkeyManager::HotkeyManager(QObject *parent) : QObject(parent) {}

void HotkeyManager::registerSaveClip(const std::function<void()> &action)
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
		obs_key_combination_t defaultCombo = {0, OBS_KEY_F8};
		obs_hotkey_load_bindings(id, &defaultCombo, 1);
		saveClipId_ = static_cast<uint32_t>(id);
	}
}

} // namespace MoonLit
