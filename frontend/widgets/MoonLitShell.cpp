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
#include "MoonLitGamePickerDialog.hpp"
#include "MoonLitLibraryWidget.hpp"
#include "MoonLitMixer.hpp"
#include "MoonLitNavBar.hpp"
#include "MoonLitStarfield.hpp"

#include <moonlit/ui/MoonLitSettingsDialog.hpp>

#ifdef MOONLIT_BUILD
#include <moonlit/hotkeys/HotkeyManager.hpp>
#endif
#ifdef _WIN32
#include <moonlit/capture/CaptureController.hpp>
#endif

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QStringList>
#include <QUrl>

#include <algorithm>

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

} // namespace

void OBSBasic::ShowMoonLitLibrary()
{
	if (!moonlitStack || !moonlitLibrary) {
		return;
	}
	moonlitLibrary->refresh();
	moonlitStack->setCurrentWidget(moonlitLibrary);
	if (moonlitNav) {
		moonlitNav->setActiveItem(MoonLitNavBar::Item::Library);
	}
}

void OBSBasic::ShowMoonLitDashboard()
{
	if (!moonlitStack || !moonlitDashboard) {
		return;
	}
	moonlitStack->setCurrentWidget(moonlitDashboard);
	if (moonlitNav) {
		moonlitNav->setActiveItem(MoonLitNavBar::Item::Home);
	}
}

void OBSBasic::OpenMoonLitSettings()
{
	MoonLitSettingsDialog dialog(this, moonlitHotkeys);
	if (dialog.exec() == QDialog::Accepted && moonlitCaptureController) {
		/* Ajustes can edit the remembered game list. */
		moonlitCaptureController->reloadGameList();
	}
}

void OBSBasic::InitializeMoonLitShell()
{
	if (!moonlitDashboard) {
		setWindowTitle(QStringLiteral("MoonLit"));
		setObjectName(QStringLiteral("MoonLitMainWindow"));

#ifdef MOONLIT_BUILD
		/* Medal-style clip hotkey: works while a game has focus through the
		 * low-level obs hotkey delivery (GetAsyncKeyState polling, no hooks:
		 * anti-cheat safe). */
		moonlitHotkeys = new MoonLit::HotkeyManager(this);
#endif

		/* Root surface: the starfield paints the asphalt sky behind the
		 * translucent surfaces of the nav rail and the content views. */
		moonlitRoot = new MoonLitStarfield(this);
		moonlitRoot->setObjectName(QStringLiteral("moonlitRoot"));
		auto *rootLayout = new QHBoxLayout(moonlitRoot);
		rootLayout->setContentsMargins(0, 0, 0, 0);
		rootLayout->setSpacing(0);

		moonlitNav = new MoonLitNavBar(moonlitRoot);
		rootLayout->addWidget(moonlitNav);

		moonlitStack = new QStackedWidget(moonlitRoot);
		moonlitDashboard = new MoonLitDashboard(moonlitStack);
		moonlitLibrary = new MoonLitLibraryWidget(moonlitStack);
		moonlitStack->addWidget(moonlitDashboard);
		moonlitStack->addWidget(moonlitLibrary);
		rootLayout->addWidget(moonlitStack, 1);

		ui->previewLayout->addWidget(moonlitRoot);

		connect(moonlitNav, &MoonLitNavBar::homeRequested, this, &OBSBasic::ShowMoonLitDashboard);
		connect(moonlitNav, &MoonLitNavBar::libraryRequested, this, &OBSBasic::ShowMoonLitLibrary);
		connect(moonlitNav, &MoonLitNavBar::settingsRequested, this, &OBSBasic::OpenMoonLitSettings);

		connect(moonlitDashboard, &MoonLitDashboard::replayActionRequested, this,
			[this]() { ReplayBufferActionTriggered(); });
		connect(moonlitDashboard, &MoonLitDashboard::saveClipRequested, this, &OBSBasic::ReplayBufferSave);
		connect(moonlitDashboard, &MoonLitDashboard::settingsRequested, this, &OBSBasic::OpenMoonLitSettings);
		connect(moonlitDashboard, &MoonLitDashboard::fullscreenModeRequested, this,
			[this](bool enabled) {
				if (moonlitCaptureController) {
					moonlitCaptureController->setFullscreenMode(enabled);
				}
			});
		connect(moonlitDashboard, &MoonLitDashboard::gamePickRequested, this, [this]() {
			if (!moonlitCaptureController) {
				return;
			}
			MoonLitGamePickerDialog dialog(this);
			if (dialog.exec() != QDialog::Accepted) {
				return;
			}
			const MoonLitTarget target = dialog.selectedTarget();
			if (!target.isValid()) {
				return;
			}
			if (dialog.rememberRequested()) {
				moonlitCaptureController->rememberGame(target.executablePath, true);
			}
			moonlitCaptureController->selectGame(target);
		});
		connect(moonlitDashboard, &MoonLitDashboard::recentClipRequested, this,
			[this](const QString &id, const QString &path) {
				/* Medal-style: a recent clip opens directly with the default
				 * player; only missing clips fall back to the library. */
				if (!path.isEmpty()) {
					QDesktopServices::openUrl(QUrl::fromLocalFile(path));
					return;
				}
				ShowMoonLitLibrary();
				moonlitLibrary->selectClip(id);
			});
		connect(moonlitLibrary, &MoonLitLibraryWidget::libraryUpdated, moonlitDashboard,
			[this](const QVector<MoonLit::Clip> &clips) {
				QVector<MoonLit::Clip> recent = clips;
				std::sort(recent.begin(), recent.end(),
					  [](const MoonLit::Clip &left, const MoonLit::Clip &right) {
						  return left.createdAtUtc > right.createdAtUtc;
					  });
				moonlitDashboard->setRecentClips(recent);
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
					/* Same hint OBS Studio shows when Defender's
					 * ransomware protection blocks the output folder. */
					message = QStringLiteral(
						"fallo al guardar el clip. Si la proteccion contra ransomware de Windows esta "
						"activada (acceso controlado a carpetas), puede causar este error: cambia la "
						"carpeta de grabacion en Ajustes.");
				moonlitDashboard->setClipError(message);
			});

		connect(this, &OBSBasic::ReplayBufStarted, this,
			[this]() {
				if (moonlitCaptureController) {
					moonlitCaptureController->onReplayStarted();
				}
				moonlitDashboard->setReplayState(true);
			});
		connect(this, &OBSBasic::ReplayBufStopping, this,
			[this]() {
				if (moonlitCaptureController) {
					moonlitCaptureController->onReplayStopping();
				}
				moonlitDashboard->setReplayState(true, true);
			});
		connect(this, &OBSBasic::ReplayBufStopped, this,
			[this]() {
				if (moonlitCaptureController) {
					moonlitCaptureController->onReplayStopped();
				}
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

#ifdef MOONLIT_BUILD
	/* Register the save-clip hotkey here, not in the constructor pass: the
	 * constructor runs before InitBasicConfig loads the profile, so
	 * Config() is still null there and the binding could not be restored. */
	if (moonlitHotkeys && Config()) {
		moonlitHotkeys->registerSaveClip(Config(), [this]() { ReplayBufferSave(); });
	}
#endif

	moonlitDashboard->setReplayState(ReplayBufferActive());
	moonlitDashboard->setCaptureStatus(QStringLiteral("fuente OBS existente"));
	moonlitDashboard->setEncoderStatus(availableVideoEncoders());
}

/* MoonLit::ICaptureHost */

OBSScene OBSBasic::moonlitCurrentScene()
{
	return GetCurrentScene();
}

bool OBSBasic::replayBufferActive()
{
	return ReplayBufferActive();
}

bool OBSBasic::startReplayBuffer(bool silent)
{
	return StartReplayBufferImpl(silent);
}

void OBSBasic::stopReplayBuffer(bool silent)
{
	StopReplayBufferImpl(silent);
}

config_t *OBSBasic::activeConfig()
{
	return activeConfiguration;
}

#ifdef _WIN32
void OBSBasic::InitializeMoonLitDetection()
{
	if (moonlitCaptureController) {
		return;
	}

	moonlitCaptureController = new MoonLit::CaptureController(this);
	moonlitCaptureController->setHost(this);
	moonlitCaptureController->setDashboard(moonlitDashboard);
	moonlitCaptureController->start();
}

void OBSBasic::UpdateMoonLitMixer()
{
	if (moonlitCaptureController) {
		moonlitCaptureController->refreshMixer();
	}
}

void OBSBasic::ApplyMoonLitNoiseSuppression()
{
	if (moonlitCaptureController) {
		moonlitCaptureController->applyNoiseSuppression();
	}
}
#endif
