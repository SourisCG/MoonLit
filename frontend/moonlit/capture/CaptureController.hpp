#pragma once

#include "ICaptureBackend.hpp"
#include "ICaptureHost.hpp"

#include <moonlit/capture/CaptureStateMachine.hpp>

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
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

private slots:
	void onTargetDetected(const MoonLitTarget &target);
	void onTargetFocusChanged(bool focused);
	void onTargetLost();
	void onHealthTick();

private:
	CaptureTarget toCaptureTarget(const MoonLitTarget &target) const;
	void configure(const CaptureTarget &target);
	void tryMonitorFallback();
	void clear();
	void setStatus(const QString &status);
	void setGame(const QString &game);

	ICaptureHost *host_ = nullptr;
	QPointer<MoonLitDashboard> dashboard_;
	std::unique_ptr<ICaptureBackend> backend_;
	CaptureStateMachine machine_;
	MoonLitGameDetector *detector_ = nullptr;
	QTimer *healthTimer_ = nullptr;

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
