#pragma once

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

	/* Registers "MoonLit.SaveClip" bound to F8 by default. The action is
	 * invoked on the UI thread, safe to call from the hotkey thread. */
	void registerSaveClip(const std::function<void()> &action);

private:
	std::function<void()> action_;
	uint32_t saveClipId_ = 0; /* 0 == OBS_INVALID_HOTKEY_ID */
};

} // namespace MoonLit
