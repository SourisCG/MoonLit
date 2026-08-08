#pragma once

#include "EndpointVolume.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QSlider;
class QSpinBox;
class OBSBasic;
class OBSHotkeyEdit;
class MoonLitStarfield;

namespace MoonLit {
class HotkeyManager;
}

/* MoonLit-specific output settings dialog. Replaces the stock OBS settings
 * dialog from the MoonLit dashboard. Persists through the existing config
 * keys consumed by SimpleOutput/AdvancedOutput. */
class MoonLitSettingsDialog final : public QDialog {
	Q_OBJECT

public:
	explicit MoonLitSettingsDialog(OBSBasic *main, MoonLit::HotkeyManager *hotkeys,
				       QWidget *parent = nullptr);

private slots:
	void BrowseOutputPath();
	void SaveAndAccept();

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	void LoadCurrentValues();
	void SaveValues();
	void PopulatePresetCombo();
	void PopulateVolumeRow(QLabel *label, QSlider *slider, QPushButton *mute,
			       MoonLit::EndpointVolume *endpoint, MoonLit::EndpointVolume::Direction direction,
			       const QString &deviceId);

	OBSBasic *main_ = nullptr;
	MoonLit::HotkeyManager *hotkeys_ = nullptr;
	MoonLitStarfield *starfield_ = nullptr;

	QComboBox *encoderCombo = nullptr;
	QComboBox *qualityCombo = nullptr;
	QComboBox *presetCombo = nullptr;
	QSpinBox *replaySeconds = nullptr;
	QSpinBox *replaySizeMb = nullptr;
	QCheckBox *trackMixed = nullptr;
	QCheckBox *trackGame = nullptr;
	QCheckBox *trackMic = nullptr;
	QCheckBox *trackChat = nullptr;
	QLineEdit *outputPath = nullptr;
	QComboBox *micDevice = nullptr;
	QComboBox *desktopDevice = nullptr;
	QLineEdit *chatExe = nullptr;
	QCheckBox *autoStart = nullptr;
	QCheckBox *minimizeToTray = nullptr;
	QCheckBox *clipSound = nullptr;
	QCheckBox *noiseSuppression = nullptr;

	/* Physical endpoint volume of the selected input/output devices. These
	 * affect what the user hears in Windows, not the recording levels. */
	QLabel *micVolumeLabel = nullptr;
	QSlider *micVolumeSlider = nullptr;
	QPushButton *micMute = nullptr;
	QLabel *desktopVolumeLabel = nullptr;
	QSlider *desktopVolumeSlider = nullptr;
	QPushButton *desktopMute = nullptr;
	MoonLit::EndpointVolume micEndpoint_;
	MoonLit::EndpointVolume desktopEndpoint_;

	/* Remembered games (MoonLit.GameList): picked manually and re-detected
	 * automatically afterwards. */
	QListWidget *gameListWidget = nullptr;
	QPushButton *removeGameButton = nullptr;

	/* Save-clip hotkey (MoonLit.SaveClip): key combination with optional
	 * modifiers (Ctrl+F8, Alt+Shift+F8, ...), default F8. */
	OBSHotkeyEdit *saveClipHotkeyEdit = nullptr;
	QPushButton *resetHotkeyButton = nullptr;
};
