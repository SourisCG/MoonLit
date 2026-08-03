#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class OBSBasic;

/* MoonLit-specific output settings dialog. Replaces the stock OBS settings
 * dialog from the MoonLit dashboard. Persists through the existing config
 * keys consumed by SimpleOutput/AdvancedOutput. */
class MoonLitSettingsDialog final : public QDialog {
	Q_OBJECT

public:
	explicit MoonLitSettingsDialog(OBSBasic *main, QWidget *parent = nullptr);

private slots:
	void BrowseOutputPath();
	void SaveAndAccept();

private:
	void LoadCurrentValues();
	void SaveValues();

	OBSBasic *main_ = nullptr;

	QComboBox *encoderCombo = nullptr;
	QSpinBox *replaySeconds = nullptr;
	QSpinBox *replaySizeMb = nullptr;
	QCheckBox *trackMixed = nullptr;
	QCheckBox *trackGame = nullptr;
	QCheckBox *trackMic = nullptr;
	QCheckBox *trackChat = nullptr;
	QLineEdit *outputPath = nullptr;
	QLineEdit *micDevice = nullptr;
	QLineEdit *chatExe = nullptr;
	QCheckBox *autoStart = nullptr;
	QCheckBox *clipSound = nullptr;
};
