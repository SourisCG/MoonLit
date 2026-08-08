#pragma once

#include <obs.hpp>

#include <util/config-file.h>

#include <QObject>

#include <cstdint>
#include <functional>

namespace MoonLit {

/* Frontend hotkey surface. On Windows the binding is registered through the
 * libobs hotkey system and delivered by the engine's GetAsyncKeyState polling
 * (no hooks, no injection, anti-cheat safe). The interface exists so a future
 * X11/XWayland or Wayland portal backend (xdg-desktop-portal
 * GlobalShortcuts) can replace the delivery without touching the UI. */
class HotkeyManager final : public QObject {
	Q_OBJECT

public:
	explicit HotkeyManager(QObject *parent = nullptr);

	/* Registers "MoonLit.SaveClip", bound to F8 by default. Any binding the
	 * user has saved under "Hotkeys"/"MoonLit.SaveClip" is restored; the
	 * action is invoked on the UI thread, safe to call from the hotkey
	 * thread. */
	void registerSaveClip(config_t *config, const std::function<void()> &action);

	/* Replaces the save-clip binding (a key combination with optional
	 * Ctrl/Shift/Alt/Win modifiers, e.g. Ctrl+F8) and persists it to the
	 * given config so it survives restarts. An empty combination unbinds. */
	void setSaveClipHotkey(config_t *config, obs_key_combination_t combo);

	/* Current save-clip key combination, or {0, OBS_KEY_NONE} if unbound. */
	obs_key_combination_t saveClipHotkey() const;

private:
	std::function<void()> action_;
	uint32_t saveClipId_ = 0; /* 0 == OBS_INVALID_HOTKEY_ID */
};

} // namespace MoonLit
