#pragma once

#include "ICaptureBackend.hpp"
#include "ICaptureHost.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace MoonLit {

/* Windows capture backend: WGC window capture (method 2) with HWND + PID +
 * process creation time identity, four audio sources and the black privacy
 * shield, plus the DXGI monitor fallback. */
class WindowsCaptureBackend final : public ICaptureBackend {
public:
	explicit WindowsCaptureBackend(ICaptureHost *host);

	BackendKind kind() const override { return BackendKind::Wgc; }
	bool attachWindow(const CaptureTarget &target) override;
	bool attachMonitorFallback(const CaptureTarget &target) override;
	bool hasCapture() const override { return captureSource_ != nullptr; }
	bool hasVideo() const override;
	CaptureHealth health() const override;
	void shield() override;
	void cover() override;
	void reveal() override;
	void detach() override;
	void setProcessAudioEnabled(bool enabled) override;
	void applyNoiseSuppression() override;
	bool monitorFallbackIsSafe(const CaptureTarget &target) const override;

	OBSSource desktopSource() const override { return desktopSource_; }
	OBSSource gameSource() const override { return audioSource_; }
	OBSSource micSource() const override { return micSource_; }
	OBSSource chatSource() const override { return chatSource_; }

private:
	bool installShield();
	void setBounds(obs_sceneitem_t *item);
	OBSSource createGameAudioSource(const std::string &selector, const CaptureTarget &target);
	OBSSource createMicSource();
	OBSSource createChatSource();
	OBSSource createDesktopSource();
	void removeAudioItems();

	ICaptureHost *host_ = nullptr;
	OBSSceneItem shieldItem_;
	OBSSceneItem captureItem_;
	OBSSceneItem audioItem_;
	OBSSceneItem micItem_;
	OBSSceneItem chatItem_;
	OBSSceneItem desktopItem_;
	OBSSource captureSource_;
	OBSSource audioSource_;
	OBSSource micSource_;
	OBSSource chatSource_;
	OBSSource desktopSource_;
};

} // namespace MoonLit
