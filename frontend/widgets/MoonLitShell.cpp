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
#include "MoonLitLibraryWidget.hpp"
#include "MoonLitMixer.hpp"

#include <moonlit/ui/MoonLitSettingsDialog.hpp>

#ifdef MOONLIT_BUILD
#include <moonlit/hotkeys/HotkeyManager.hpp>
#endif
#ifdef _WIN32
#include <moonlit/capture/CaptureController.hpp>
#endif

#include <QStringList>

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
	if (!moonlitDashboard || !moonlitLibrary) {
		return;
	}
	moonlitDashboard->hide();
	moonlitLibrary->refresh();
	moonlitLibrary->show();
}

void OBSBasic::ShowMoonLitDashboard()
{
	if (moonlitLibrary) {
		moonlitLibrary->hide();
	}
	if (moonlitDashboard) {
		moonlitDashboard->show();
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
		moonlitHotkeys->registerSaveClip([this]() { ReplayBufferSave(); });
#endif

		moonlitDashboard = new MoonLitDashboard(this);
		ui->previewLayout->addWidget(moonlitDashboard);
		moonlitLibrary = new MoonLitLibraryWidget(this);
		ui->previewLayout->addWidget(moonlitLibrary);
		moonlitLibrary->hide();

		connect(moonlitDashboard, &MoonLitDashboard::replayActionRequested, this,
			[this]() { ReplayBufferActionTriggered(); });
		connect(moonlitDashboard, &MoonLitDashboard::saveClipRequested, this, &OBSBasic::ReplayBufferSave);
		connect(moonlitDashboard, &MoonLitDashboard::settingsRequested, this, [this]() {
			MoonLitSettingsDialog dialog(this);
			dialog.exec();
		});
		connect(moonlitDashboard, &MoonLitDashboard::libraryRequested, this, &OBSBasic::ShowMoonLitLibrary);
		connect(moonlitLibrary, &MoonLitLibraryWidget::backRequested, this, &OBSBasic::ShowMoonLitDashboard);
		connect(moonlitLibrary, &MoonLitLibraryWidget::libraryUpdated, moonlitDashboard,
			[this](const QVector<MoonLit::Clip> &clips) {
				QVector<MoonLit::Clip> recent = clips;
				std::sort(recent.begin(), recent.end(),
					  [](const MoonLit::Clip &left, const MoonLit::Clip &right) {
						  return left.createdAtUtc > right.createdAtUtc;
					  });
				moonlitDashboard->setRecentClips(recent);
			});
		connect(moonlitDashboard, &MoonLitDashboard::recentClipRequested, this,
			[this](const QString &id) {
				ShowMoonLitLibrary();
				moonlitLibrary->selectClip(id);
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
					message = QStringLiteral("fallo al guardar el clip");
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

void OBSBasic::startReplayBuffer()
{
	StartReplayBuffer();
}

void OBSBasic::stopReplayBuffer()
{
	StopReplayBuffer();
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
