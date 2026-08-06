#pragma once

#include "ICaptureBackend.hpp"
#include "ICaptureHost.hpp"
#include "WindowsProcessUtil.hpp"

#include <moonlit/capture/CaptureStateMachine.hpp>

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <memory>

class MoonLitDashboard;
class MoonLitGameDetector;
struct MoonLitTarget;

namespace MoonLit {

/* Owns the capture state flow: the game detector, the capture backend, the
 * health timer and the replay lifecycle flags. All decisions about when to
 * unshield, start the replay buffer or fall back to the monitor are made by
 * the pure CaptureStateMachine; this class only executes its actions through
 * the host and backend interfaces. */
class CaptureController final : public QObject {
	Q_OBJECT

public:
	enum class CaptureMode {
		Auto,       /* detector-driven foreground game capture */
		Fullscreen, /* whole primary monitor, no game needed */
		Manual,     /* user-pinned process, detector blocked */
	};

	explicit CaptureController(QObject *parent = nullptr);

	void setHost(ICaptureHost *host) { host_ = host; }
	void setDashboard(MoonLitDashboard *dashboard);

	/* Detector + health timer. */
	void start();
	/* Detector stop and full capture teardown (app shutdown). */
	void shutdown();

	/* Replay lifecycle signals from the host output handler. */
	void onReplayStarted();
	void onReplayStopping();
	void onReplayStopped();

	void refreshMixer();
	void applyNoiseSuppression() { if (backend_) backend_->applyNoiseSuppression(); }

	/* Manual capture modes. */
	void setFullscreenMode(bool enabled);
	void selectGame(const MoonLitTarget &target);
	/* Adds/removes the executable in MoonLit.GameList so the detector
	 * recognizes it automatically; reloads the list from config. */
	void rememberGame(const QString &executablePath, bool remember);
	void reloadGameList();

	CaptureMode mode() const { return mode_; }

private slots:
	void onTargetDetected(const MoonLitTarget &target);
	void onTargetFocusChanged(bool focused);
	void onTargetLost();
	void onHealthTick();

private:
	CaptureTarget toCaptureTarget(const MoonLitTarget &target) const;
	MoonLitTarget toMoonLitTarget(const CaptureTarget &target) const;
	void configure(const CaptureTarget &target);
	void tryMonitorFallback();
	void manualTargetLost();
	void clear();
	void setStatus(const QString &status);
	void setGame(const QString &game);
	QStringList gameListFromConfig() const;

	ICaptureHost *host_ = nullptr;
	QPointer<MoonLitDashboard> dashboard_;
	std::unique_ptr<ICaptureBackend> backend_;
	CaptureStateMachine machine_;
	MoonLitGameDetector *detector_ = nullptr;
	QTimer *healthTimer_ = nullptr;
	CaptureMode mode_ = CaptureMode::Auto;

	CaptureTarget target_;
	bool started_ = false;
	bool focused_ = false;
	bool monitorFallback_ = false;
	bool replayStartRequested_ = false;
	bool replayAutoBlocked_ = false;
	bool replayManualStopRequested_ = false;
	int startFailures_ = 0;
	QElapsedTimer wgcTimer_;
	QElapsedTimer configureRetryTimer_;
	QElapsedTimer replayRetryTimer_;
};

} // namespace MoonLit
