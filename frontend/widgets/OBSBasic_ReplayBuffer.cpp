/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
                          Zachary Lund <admin@computerquip.com>
                          Philippe Groarke <philippe.groarke@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSBasic.hpp"

#include <components/UIValidation.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>

#ifdef MOONLIT_BUILD
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

#define REPLAY_BUFFER_START "==== Replay Buffer Start ==========================================="
#define REPLAY_BUFFER_STOP "==== Replay Buffer Stop ============================================"

void OBSBasic::ReplayBufferActionTriggered()
{
	if (outputHandler->ReplayBufferActive()) {
		StopReplayBuffer();
	} else {
		StartReplayBuffer();
	}
};

void OBSBasic::ShowReplayBufferPauseWarning()
{
	auto msgBox = []() {
		QMessageBox msgbox(App()->GetMainWindow());
		msgbox.setWindowTitle(QTStr("Output.ReplayBuffer."
					    "PauseWarning.Title"));
		msgbox.setText(QTStr("Output.ReplayBuffer."
				     "PauseWarning.Text"));
		msgbox.setIcon(QMessageBox::Icon::Information);
		msgbox.addButton(QMessageBox::Ok);

		QCheckBox *cb = new QCheckBox(QTStr("DoNotShowAgain"));
		msgbox.setCheckBox(cb);

		msgbox.exec();

		if (cb->isChecked()) {
			config_set_bool(App()->GetUserConfig(), "General", "WarnedAboutReplayBufferPausing", true);
			config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
		}
	};

	bool warned = config_get_bool(App()->GetUserConfig(), "General", "WarnedAboutReplayBufferPausing");
	if (!warned) {
		QMetaObject::invokeMethod(App(), "Exec", Qt::QueuedConnection, Q_ARG(VoidFunc, msgBox));
	}
}

void OBSBasic::StartReplayBuffer()
{
	StartReplayBufferImpl(false);
}

void OBSBasic::StartReplayBufferSilently()
{
	StartReplayBufferImpl(true);
}

bool OBSBasic::StartReplayBufferImpl(bool silent)
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return false;
	}
	if (outputHandler->ReplayBufferActive()) {
		return true;
	}
	if (disableOutputsRef) {
		return false;
	}

	if (!silent && !UIValidation::NoSourcesConfirmation(this)) {
		return false;
	}

	if (!OutputPathValid()) {
		if (silent) {
			return false;
		}
		OutputPathInvalidMessage();
		return false;
	}

	if (LowDiskSpace()) {
		if (silent) {
			return false;
		}
		DiskSpaceMessage();
		return false;
	}

	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING);

	/* MoonLit capture sources are runtime-only and must not enter the saved
	 * scene collection when replay starts. */
	disableSaving++;
	SaveProject();
	disableSaving--;

	if (silent) {
		silentReplay_ = true;
	}
	const bool started = outputHandler->StartReplayBuffer();
	silentReplay_ = false;

	if (started && os_atomic_load_bool(&recording_paused) && !silent) {
		ShowReplayBufferPauseWarning();
	}
	return started;
}

void OBSBasic::ReplayBufferStopping()
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}

	emit ReplayBufStopping();

	if (sysTrayReplayBuffer) {
		sysTrayReplayBuffer->setText(QTStr("Basic.Main.StoppingReplayBuffer"));
	}

	replayBufferStopping = true;
	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING);
}

void OBSBasic::StopReplayBuffer()
{
	StopReplayBufferImpl(false);
}

void OBSBasic::StopReplayBufferSilently()
{
	StopReplayBufferImpl(true);
}

void OBSBasic::StopReplayBufferImpl(bool silent)
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}

	disableSaving++;
	SaveProject();
	disableSaving--;

	if (outputHandler->ReplayBufferActive()) {
		silentReplayStop_ = silent;
		outputHandler->StopReplayBuffer(replayBufferStopping);
	}

	OnDeactivate();
}

void OBSBasic::ReplayBufferStart()
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}

	emit ReplayBufStarted();

	if (sysTrayReplayBuffer) {
		sysTrayReplayBuffer->setText(QTStr("Basic.Main.StopReplayBuffer"));
	}

	replayBufferStopping = false;
	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED);

	OnActivate();

	blog(LOG_INFO, REPLAY_BUFFER_START);
}

void OBSBasic::ReplayBufferSave()
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}
	if (!outputHandler->ReplayBufferActive()) {
		return;
	}

	calldata_t cd = {0};
	proc_handler_t *ph = obs_output_get_proc_handler(outputHandler->replayBuffer);
	proc_handler_call(ph, "save", &cd);
	calldata_free(&cd);
}

void OBSBasic::ReplayBufferSaved()
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}
	calldata_t cd = {0};
	proc_handler_t *ph = obs_output_get_proc_handler(outputHandler->replayBuffer);
	proc_handler_call(ph, "get_last_replay", &cd);
	std::string path = calldata_string(&cd, "path");
	if (path.empty()) {
		calldata_free(&cd);
		return;
	}
	QString msg = QTStr("Basic.StatusBar.ReplayBufferSavedTo").arg(QT_UTF8(path.c_str()));
	ShowStatusBarMessage(msg);
	SysTrayNotify(QTStr("Basic.StatusBar.ReplayBufferSavedTo").arg(QT_UTF8(path.c_str())),
		      QSystemTrayIcon::Information);
#ifdef MOONLIT_BUILD
	if (config_get_bool(App()->GetUserConfig(), "MoonLit", "ClipSound")) {
		const QString soundPath = QCoreApplication::applicationDirPath() +
					  QStringLiteral("/../../data/obs-studio/sounds/moonlit-clip.wav");
		if (QFile::exists(soundPath)) {
			PlaySoundA(QDir::toNativeSeparators(soundPath).toUtf8().constData(), nullptr,
				   SND_FILENAME | SND_ASYNC);
		}
	}
#endif
	lastReplay = path;
	emit ReplayClipSaved(QT_UTF8(path.c_str()));
	calldata_free(&cd);

	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED);

	AutoRemux(QT_UTF8(path.c_str()));
}

void OBSBasic::ReplayBufferStop(int code)
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}

	emit ReplayBufStopped();

	if (sysTrayReplayBuffer) {
		sysTrayReplayBuffer->setText(QTStr("Basic.Main.StartReplayBuffer"));
	}

	blog(LOG_INFO, REPLAY_BUFFER_STOP);

	/* Automatic (silent) stops surface errors through the MoonLit dashboard
	 * instead of modal dialogs. */
	const bool silent = silentReplayStop_;
	silentReplayStop_ = false;

	if (code == OBS_OUTPUT_UNSUPPORTED) {
		emit ReplaySaveFailed(code);
		if (!silent) {
			if (isVisible()) {
				OBSMessageBox::critical(this, QTStr("Output.RecordFail.Title"),
							QTStr("Output.RecordFail.Unsupported"));
			} else {
				SysTrayNotify(QTStr("Output.RecordFail.Unsupported"), QSystemTrayIcon::Warning);
			}
		}
	} else if (code == OBS_OUTPUT_NO_SPACE) {
		emit ReplaySaveFailed(code);
		if (!silent) {
			if (isVisible()) {
				OBSMessageBox::warning(this, QTStr("Output.RecordNoSpace.Title"),
						       QTStr("Output.RecordNoSpace.Msg"));
			} else {
				SysTrayNotify(QTStr("Output.RecordNoSpace.Msg"), QSystemTrayIcon::Warning);
			}
		}
	} else if (code != OBS_OUTPUT_SUCCESS) {
		emit ReplaySaveFailed(code);
		if (!silent) {
			if (isVisible()) {
				OBSMessageBox::critical(this, QTStr("Output.RecordError.Title"),
							QTStr("Output.RecordError.Msg"));
			} else {
				SysTrayNotify(QTStr("Output.RecordError.Msg"), QSystemTrayIcon::Warning);
			}
		}
	}

	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED);

	OnDeactivate();
}

bool OBSBasic::ReplayBufferActive()
{
	if (!outputHandler) {
		return false;
	}
	return outputHandler->ReplayBufferActive();
}
