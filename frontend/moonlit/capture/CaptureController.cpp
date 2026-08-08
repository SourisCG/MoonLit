#include "CaptureController.hpp"

#include "WindowsCaptureBackend.hpp"

#include <moonlit/Clip.hpp>

#include <widgets/MoonLitDashboard.hpp>
#include <widgets/MoonLitGameDetector.hpp>
#include <widgets/MoonLitMixer.hpp>

#include <QDir>
#include <QTimer>

#include <algorithm>

namespace MoonLit {

namespace {

constexpr qint64 kReplayRetryMs = 1000;
constexpr qint64 kConfigureRetryMs = 1000;
constexpr qint64 kWgcTimeoutMs = 5000;

} // namespace

CaptureController::CaptureController(QObject *parent) : QObject(parent) {}

void CaptureController::setDashboard(MoonLitDashboard *dashboard)
{
	dashboard_ = dashboard;
}

void CaptureController::start()
{
	if (started_) {
		return;
	}

	focused_ = false;
	monitorFallback_ = false;
	replayStartRequested_ = false;
	startFailures_ = 0;
	replayAutoBlocked_ = false;
	replayManualStopRequested_ = false;

	backend_ = std::make_unique<WindowsCaptureBackend>(host_);
	refreshMixer();

	detector_ = new MoonLitGameDetector(this);
	connect(detector_, &MoonLitGameDetector::targetDetected, this, &CaptureController::onTargetDetected);
	connect(detector_, &MoonLitGameDetector::targetFocusChanged, this, &CaptureController::onTargetFocusChanged);
	connect(detector_, &MoonLitGameDetector::targetLost, this, &CaptureController::onTargetLost);
	reloadGameList();

	healthTimer_ = new QTimer(this);
	healthTimer_->setInterval(250);
	connect(healthTimer_, &QTimer::timeout, this, &CaptureController::onHealthTick);
	healthTimer_->start();
	detector_->start();
	started_ = true;
}

void CaptureController::shutdown()
{
	if (!started_) {
		return;
	}

	if (detector_) {
		detector_->blockSignals(true);
		detector_->stop();
		detector_->blockSignals(false);
	}
	if (backend_) {
		backend_->shield();
		if (host_ && host_->replayBufferActive()) {
			host_->stopReplayBuffer(true);
		}
		backend_->detach();
	}
	started_ = false;
}

void CaptureController::onReplayStarted()
{
	startFailures_ = 0;
	replayAutoBlocked_ = false;
	replayManualStopRequested_ = false;
	replayStartRequested_ = true;
}

void CaptureController::onReplayStopping()
{
	replayManualStopRequested_ = true;
}

void CaptureController::onReplayStopped()
{
	replayStartRequested_ = false;
	if (replayManualStopRequested_) {
		replayAutoBlocked_ = true;
	}
	replayManualStopRequested_ = false;
}

void CaptureController::refreshMixer()
{
	if (!dashboard_) {
		return;
	}
	MoonLitMixer *mixer = dashboard_->mixer();
	if (!mixer) {
		return;
	}
	mixer->setConfig(host_ ? host_->activeConfig() : nullptr);
	mixer->clearSources();
	/* All four rows are always shown; rows without a live source render as
	 * disabled placeholders (e.g. game audio while no game is running). */
	mixer->addSource(QStringLiteral("Escritorio"), backend_ ? backend_->desktopSource() : nullptr);
	mixer->addSource(QStringLiteral("Juego"), backend_ ? backend_->gameSource() : nullptr);
	mixer->addSource(QStringLiteral("Microfono"), backend_ ? backend_->micSource() : nullptr);
	mixer->addSource(QStringLiteral("Chat"), backend_ ? backend_->chatSource() : nullptr);
}

CaptureTarget CaptureController::toCaptureTarget(const MoonLitTarget &target) const
{
	CaptureTarget result;
	result.name = target.title.toStdString();
	result.windowClass = target.windowClass.toStdString();
	result.executablePath = target.executablePath.isEmpty() ? target.executable.toStdString()
								: target.executablePath.toStdString();
	result.processId = target.processId;
	result.creationTimeNs = target.creationTime;
	result.window = static_cast<uintptr_t>(target.window);
	return result;
}

MoonLitTarget CaptureController::toMoonLitTarget(const CaptureTarget &target) const
{
	MoonLitTarget result;
	if (const auto *handle = std::get_if<uintptr_t>(&target.window)) {
		result.window = *handle;
	}
	result.processId = static_cast<quint32>(target.processId);
	result.creationTime = target.creationTimeNs;
	result.title = QString::fromStdString(target.name);
	result.executablePath = QString::fromStdString(target.executablePath);
	result.executable = result.executablePath.section(QChar('\\'), -1);
	return result;
}

void CaptureController::onTargetDetected(const MoonLitTarget &target)
{
	if (mode_ != CaptureMode::Auto) {
		return;
	}
	configure(toCaptureTarget(target));
}

void CaptureController::onTargetFocusChanged(bool focused)
{
	if (mode_ != CaptureMode::Auto) {
		return;
	}
	focused_ = focused;
	if (backend_) {
		backend_->cover();
		backend_->setProcessAudioEnabled(focused);
	}
	if (focused && !monitorFallback_) {
		wgcTimer_.restart();
	}
	setStatus(focused ? (monitorFallback_ ? QStringLiteral("DXGI monitor fallback")
					      : QStringLiteral("captura de ventana inicializando"))
			  : QStringLiteral("pausada temporalmente (Alt+Tab)"));
}

void CaptureController::onTargetLost()
{
	if (mode_ != CaptureMode::Auto) {
		return;
	}
	focused_ = false;
	setGame(QString());
	setStatus(QStringLiteral("juego cerrado"));
	/* Hide the capture before requesting the asynchronous output stop. */
	if (backend_) {
		backend_->shield();
	}
	if (host_ && host_->replayBufferActive()) {
		host_->stopReplayBuffer(true);
	}
	clear();
}

void CaptureController::onHealthTick()
{
	if (!host_ || host_->isClosing() || !focused_) {
		return;
	}

	/* Manual mode owns the target liveness check (the detector is stopped):
	 * when the pinned process dies, tear the capture down and return to the
	 * automatic mode. */
	if (mode_ == CaptureMode::Manual && target_.isValid() &&
	    !WindowsProcessUtil::processAlive(toMoonLitTarget(target_))) {
		manualTargetLost();
		return;
	}

	CaptureTickInput input;
	input.closing = false;
	input.focused = focused_;
	input.hasCaptureSource = backend_ && backend_->hasCapture();
	input.targetValid = target_.isValid();
	input.configureRetryElapsed = !configureRetryTimer_.isValid() || configureRetryTimer_.elapsed() >= kConfigureRetryMs;
	input.monitorFallback = monitorFallback_;
	input.fallbackReady = backend_ && backend_->hasVideo();
	const CaptureHealth health = backend_ ? backend_->health() : CaptureHealth{};
	input.healthAvailable = health.active || health.firstFrameReceived;
	input.healthActive = health.active;
	input.healthFirstFrame = health.firstFrameReceived;
	input.healthWgc = health.activeKind == BackendKind::Wgc;
	input.replayActive = host_->replayBufferActive();
	input.replayStartRequested = replayStartRequested_;
	input.replayAutoBlocked = replayAutoBlocked_;
	input.replayRetryElapsed = !replayRetryTimer_.isValid() || replayRetryTimer_.elapsed() >= kReplayRetryMs;
	input.wgcElapsed5s = !wgcTimer_.isValid() || wgcTimer_.elapsed() >= kWgcTimeoutMs;
	input.monitorFallbackSafe = backend_ && backend_->monitorFallbackIsSafe(target_);

	switch (machine_.decideTick(input)) {
	case TickAction::ConfigureRetry:
		configureRetryTimer_.restart();
		configure(target_);
		break;
	case TickAction::StatusInitializing:
		setStatus(QStringLiteral("captura de ventana inicializando"));
		break;
	case TickAction::WgcReady:
		if (backend_) {
			backend_->reveal();
		}
		setStatus(QStringLiteral("WGC de ventana"));
		break;
	case TickAction::StartReplay:
		if (backend_) {
			backend_->reveal();
		}
		setStatus(mode_ == CaptureMode::Fullscreen
				  ? QStringLiteral("pantalla completa")
				  : monitorFallback_ ? QStringLiteral("DXGI monitor fallback")
						     : QStringLiteral("WGC de ventana"));
		if (!host_->replayBufferActive() && !replayStartRequested_ && !replayAutoBlocked_) {
			replayRetryTimer_.restart();
			replayStartRequested_ = true;
			if (!host_->startReplayBuffer(true)) {
				replayStartRequested_ = false;
				replayAutoBlocked_ = ++startFailures_ >= 3;
				setStatus(replayAutoBlocked_
					  ? QStringLiteral("grabacion automatica bloqueada tras 3 fallos (revisa la carpeta de grabacion y el espacio en disco)")
					  : QStringLiteral("error al iniciar la grabacion, reintentando"));
			}
		}
		break;
	case TickAction::ResetReplayFailures:
		startFailures_ = 0;
		replayAutoBlocked_ = false;
		replayStartRequested_ = true;
		break;
	case TickAction::MonitorFallbackReady:
		if (backend_) {
			backend_->reveal();
		}
		setStatus(mode_ == CaptureMode::Fullscreen ? QStringLiteral("pantalla completa")
							  : QStringLiteral("DXGI monitor fallback"));
		break;
	case TickAction::FallbackBlocked:
		if (backend_) {
			backend_->cover();
		}
		setStatus(QStringLiteral("fallback bloqueado: ventana no cubre el monitor"));
		wgcTimer_.restart();
		break;
	case TickAction::TryMonitorFallback:
		if (backend_) {
			backend_->cover();
		}
		tryMonitorFallback();
		break;
	default:
		break;
	}
}

void CaptureController::configure(const CaptureTarget &target)
{
	if (!target.isValid()) {
		return;
	}

	clear();
	target_ = target;
	focused_ = true;
	monitorFallback_ = false;
	replayStartRequested_ = false;
	startFailures_ = 0;
	replayAutoBlocked_ = false;
	wgcTimer_.restart();
	configureRetryTimer_.restart();

	if (!backend_ || !backend_->attachWindow(target)) {
		setStatus(QStringLiteral("captura de ventana no disponible"));
		return;
	}

	setGame(QString::fromStdString(target.executablePath));
	setStatus(QStringLiteral("captura de ventana inicializando"));
	refreshMixer();
}

void CaptureController::setFullscreenMode(bool enabled)
{
	if ((mode_ == CaptureMode::Fullscreen) == enabled) {
		return;
	}

	if (enabled) {
		mode_ = CaptureMode::Fullscreen;
		if (detector_) {
			detector_->stop();
		}
		clear();
		focused_ = true;
		monitorFallback_ = true;
		replayStartRequested_ = false;
		startFailures_ = 0;
		replayAutoBlocked_ = false;
		target_ = {};
		target_.name = "Pantalla completa";

		if (!backend_ || !backend_->attachFullscreen()) {
			setStatus(QStringLiteral("pantalla completa no disponible"));
			mode_ = CaptureMode::Auto;
			if (detector_) {
				detector_->start();
			}
			if (dashboard_) {
				dashboard_->setFullscreenActive(false);
			}
			return;
		}

		setGame(QStringLiteral("Pantalla completa"));
		setStatus(QStringLiteral("pantalla completa inicializando"));
		refreshMixer();
	} else {
		mode_ = CaptureMode::Auto;
		if (host_ && host_->replayBufferActive()) {
			host_->stopReplayBuffer(true);
		}
		clear();
		setGame(QString());
		setStatus(QStringLiteral("esperando juego"));
		if (detector_) {
			detector_->start();
		}
	}
	if (dashboard_) {
		dashboard_->setFullscreenActive(enabled);
	}
}

void CaptureController::selectGame(const MoonLitTarget &target)
{
	if (!target.isValid()) {
		return;
	}

	mode_ = CaptureMode::Manual;
	if (detector_) {
		detector_->stop();
	}
	configure(toCaptureTarget(target));
	if (dashboard_) {
		dashboard_->setFullscreenActive(false);
	}
}

void CaptureController::rememberGame(const QString &executablePath, bool remember)
{
	config_t *config = host_ ? host_->activeConfig() : nullptr;
	if (!config) {
		return;
	}

	QStringList list = gameListFromConfig();
	const QString canonical = QDir::fromNativeSeparators(executablePath);
	if (remember) {
		bool exists = false;
		for (const QString &entry : list) {
			if (QDir::fromNativeSeparators(entry).compare(canonical, Qt::CaseInsensitive) == 0) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			list.append(executablePath);
		}
	} else {
		list.erase(std::remove_if(list.begin(), list.end(),
					  [&canonical](const QString &entry) {
						  return QDir::fromNativeSeparators(entry)
								 .compare(canonical, Qt::CaseInsensitive) == 0;
					  }),
			   list.end());
	}

	config_set_string(config, "MoonLit", "GameList", list.join(QChar('\n')).toUtf8().constData());
	reloadGameList();
}

void CaptureController::reloadGameList()
{
	if (detector_) {
		detector_->setManualGameList(gameListFromConfig());
	}
}

QStringList CaptureController::gameListFromConfig() const
{
	config_t *config = host_ ? host_->activeConfig() : nullptr;
	if (!config) {
		return {};
	}
	const char *raw = config_get_string(config, "MoonLit", "GameList");
	if (!raw || !*raw) {
		return {};
	}
	return QString::fromUtf8(raw).split(QChar('\n'), Qt::SkipEmptyParts);
}

void CaptureController::manualTargetLost()
{
	mode_ = CaptureMode::Auto;
	setGame(QString());
	setStatus(QStringLiteral("proceso cerrado"));
	/* Hide the capture before requesting the asynchronous output stop. */
	if (backend_) {
		backend_->shield();
	}
	if (host_ && host_->replayBufferActive()) {
		host_->stopReplayBuffer(true);
	}
	clear();
	if (detector_) {
		detector_->start();
	}
	if (dashboard_) {
		dashboard_->setFullscreenActive(false);
	}
}

void CaptureController::tryMonitorFallback()
{
	if (!host_ || !backend_) {
		return;
	}

	OBSScene scene = host_->moonlitCurrentScene();
	if (!scene) {
		return;
	}

	/* Remove the window capture before installing the monitor source. */
	backend_->detach();
	if (!backend_->attachMonitorFallback(target_)) {
		wgcTimer_.restart();
		setStatus(QStringLiteral("captura segura no disponible"));
		return;
	}

	backend_->cover();
	monitorFallback_ = true;
	setStatus(QStringLiteral("fallback de monitor inicializando"));
}

void CaptureController::clear()
{
	if (backend_) {
		backend_->shield();
		backend_->detach();
	}
	target_ = {};
	monitorFallback_ = false;
	replayStartRequested_ = false;
	startFailures_ = 0;
	replayAutoBlocked_ = false;
	replayManualStopRequested_ = false;
	wgcTimer_.invalidate();
	replayRetryTimer_.invalidate();
	configureRetryTimer_.invalidate();
	if (dashboard_) {
		if (MoonLitMixer *mixer = dashboard_->mixer()) {
			mixer->clearSources();
		}
	}
}

void CaptureController::setStatus(const QString &status)
{
	if (dashboard_) {
		dashboard_->setCaptureStatus(status);
	}
}

void CaptureController::setGame(const QString &game)
{
	if (dashboard_) {
		dashboard_->setDetectedGame(game);
	}
}

} // namespace MoonLit
