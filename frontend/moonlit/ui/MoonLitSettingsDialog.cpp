#include "MoonLitSettingsDialog.hpp"

#include <moonlit/output/EncoderResolver.hpp>

#include <widgets/OBSBasic.hpp>

#include <obs.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include <string>

#include <set>

namespace {

/* Registry key used for per-user login startup (HKCU\...\Run). */
QString AutoStartRegistryValue()
{
	return QStringLiteral("\"%1\" --minimize-to-tray")
		.arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}

bool IsAutoStartEnabled()
{
	QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
			   QSettings::NativeFormat);
	return settings.contains(QStringLiteral("MoonLit"));
}

void SetAutoStartEnabled(bool enabled)
{
	QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
			   QSettings::NativeFormat);
	if (enabled) {
		settings.setValue(QStringLiteral("MoonLit"), AutoStartRegistryValue());
	} else {
		settings.remove(QStringLiteral("MoonLit"));
	}
}

/* Maps an obs encoder id back to the simple output token used in config.
 * Encoders without a simple-output token (e.g. ffmpeg svt/aom) keep their
 * obs id: the resolver treats those ids directly and falls back if the
 * encoder cannot be created at runtime. */
std::string EncoderIdToToken(const std::string &id)
{
	if (id == "obs_nvenc_h264_tex" || id == "ffmpeg_nvenc" || id == "obs_nvenc")
		return "nvenc";
	if (id == "obs_nvenc_hevc_tex" || id == "ffmpeg_hevc_nvenc")
		return "nvenc_hevc";
	if (id == "obs_nvenc_av1_tex")
		return "nvenc_av1";
	if (id == "obs_qsv11_v2" || id == "obs_qsv11")
		return "qsv";
	if (id == "obs_qsv11_av1")
		return "qsv_av1";
	if (id == "h264_texture_amf" || id == "ffmpeg_amf_h264")
		return "amd";
	if (id == "h265_texture_amf")
		return "amd_hevc";
	if (id == "av1_texture_amf")
		return "amd_av1";
	if (id == "obs_x264")
		return "x264";
	return id;
}

} /* namespace */

MoonLitSettingsDialog::MoonLitSettingsDialog(OBSBasic *main, QWidget *parent) : QDialog(parent), main_(main)
{
	setWindowTitle(QStringLiteral("Ajustes de MoonLit"));
	setModal(true);
	setMinimumWidth(420);

	encoderCombo = new QComboBox(this);
	encoderCombo->addItem(QStringLiteral("Auto (recomendar)"), QString());

	/* Every video encoder OBS has registered, not just the fallback chain:
	 * NVENC/QSV/AMF in H.264, HEVC and AV1, x264, and ffmpeg svt/aom. */
	std::set<std::string> seenTokens;
	const char *encoderId = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &encoderId); ++index) {
		if (obs_get_encoder_type(encoderId) != OBS_ENCODER_VIDEO)
			continue;

		const std::string token = EncoderIdToToken(encoderId);
		if (!seenTokens.insert(token).second)
			continue;

		const char *display = obs_encoder_get_display_name(encoderId);
		encoderCombo->addItem(QString::fromUtf8(display ? display : encoderId),
				      QString::fromStdString(token));
	}

	replaySeconds = new QSpinBox(this);
	replaySeconds->setRange(1, 600);
	replaySeconds->setSuffix(QStringLiteral(" s"));

	replaySizeMb = new QSpinBox(this);
	replaySizeMb->setRange(0, 10240);
	replaySizeMb->setSuffix(QStringLiteral(" MB"));

	trackMixed = new QCheckBox(QStringLiteral("Mezcla"), this);
	trackGame = new QCheckBox(QStringLiteral("Juego"), this);
	trackMic = new QCheckBox(QStringLiteral("Micrófono"), this);
	trackChat = new QCheckBox(QStringLiteral("Chat"), this);

	outputPath = new QLineEdit(this);
	QPushButton *browse = new QPushButton(QStringLiteral("Examinar…"), this);
	connect(browse, &QPushButton::clicked, this, &MoonLitSettingsDialog::BrowseOutputPath);

	micDevice = new QLineEdit(this);
	micDevice->setPlaceholderText(QStringLiteral("default"));

	chatExe = new QLineEdit(this);
	chatExe->setPlaceholderText(QStringLiteral("Discord.exe"));

	autoStart = new QCheckBox(QStringLiteral("Iniciar MoonLit con Windows (oculto en la bandeja)"), this);

	clipSound = new QCheckBox(QStringLiteral("Sonido al guardar clip"), this);

	QLabel *formatNote = new QLabel(
		QStringLiteral("Formato de grabación: MKV (autoritativo; MP4 solo como exportación)."), this);
	formatNote->setWordWrap(true);

	QLabel *audioNote = new QLabel(
		QStringLiteral("Pistas: 1 mezcla (audio de escritorio de la escena), 2 juego, 3 micrófono, 4 chat."),
		this);
	audioNote->setWordWrap(true);

	QGroupBox *aboutGroup = new QGroupBox(QStringLiteral("Acerca de MoonLit"), this);
	QLabel *aboutText = new QLabel(
		QStringLiteral("MoonLit 1.0.0\nGrabadora de clips local basada en OBS Studio 32.2.1.\n"
			       "Software libre bajo GPLv2."),
		aboutGroup);
	aboutText->setWordWrap(true);
	QVBoxLayout *aboutLayout = new QVBoxLayout(aboutGroup);
	aboutLayout->addWidget(aboutText);

	QHBoxLayout *pathLayout = new QHBoxLayout;
	pathLayout->addWidget(outputPath, 1);
	pathLayout->addWidget(browse);

	QFormLayout *form = new QFormLayout;
	form->addRow(QStringLiteral("Encoder de vídeo:"), encoderCombo);
	form->addRow(QStringLiteral("Duración del buffer:"), replaySeconds);
	form->addRow(QStringLiteral("Tamaño máximo:"), replaySizeMb);
	QLabel *tracksHeader = new QLabel(QStringLiteral("Pistas de audio:"), this);
	form->addRow(tracksHeader, (QLayout *)nullptr);
	form->addRow(QStringLiteral("Pista 1 (mezcla):"), trackMixed);
	form->addRow(QStringLiteral("Pista 2 (juego):"), trackGame);
	form->addRow(QStringLiteral("Pista 3 (micrófono):"), trackMic);
	form->addRow(QStringLiteral("Pista 4 (chat):"), trackChat);
	form->addRow(QStringLiteral("Carpeta de grabación:"), pathLayout);
	form->addRow(audioNote);
	form->addRow(QStringLiteral("Micrófono (ID de dispositivo):"), micDevice);
	form->addRow(QStringLiteral("Chat (ejecutable):"), chatExe);
	form->addRow(autoStart);
	form->addRow(clipSound);
	form->addRow(formatNote);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &MoonLitSettingsDialog::SaveAndAccept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(aboutGroup);
	layout->addWidget(buttons);

	LoadCurrentValues();
}

void MoonLitSettingsDialog::BrowseOutputPath()
{
	const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Carpeta de grabación"),
							      outputPath->text());
	if (!dir.isEmpty()) {
		outputPath->setText(QDir::toNativeSeparators(dir));
	}
}

void MoonLitSettingsDialog::LoadCurrentValues()
{
	config_t *config = main_->Config();

	const char *savedToken = config_get_string(config, "SimpleOutput", "RecEncoder");
	const int savedIndex = encoderCombo->findData(QString::fromUtf8(savedToken ? savedToken : ""));
	if (savedIndex >= 0) {
		encoderCombo->setCurrentIndex(savedIndex);
	}

	replaySeconds->setValue(config_get_int(config, "SimpleOutput", "RecRBTime"));
	replaySizeMb->setValue(config_get_int(config, "SimpleOutput", "RecRBSize"));

	const uint32_t tracks = config_get_uint(config, "SimpleOutput", "RecTracks");
	trackMixed->setChecked(tracks & (1u << 0));
	trackGame->setChecked(tracks & (1u << 1));
	trackMic->setChecked(tracks & (1u << 2));
	trackChat->setChecked(tracks & (1u << 3));

	const char *path = config_get_string(config, "SimpleOutput", "FilePath");
	outputPath->setText(QString::fromUtf8(path ? path : ""));

	const char *mic = config_get_string(config, "MoonLit", "MicDeviceId");
	micDevice->setText(QString::fromUtf8(mic ? mic : "default"));

	const char *chat = config_get_string(config, "MoonLit", "ChatExe");
	chatExe->setText(QString::fromUtf8(chat ? chat : ""));

	autoStart->setChecked(IsAutoStartEnabled());

	clipSound->setChecked(config_get_bool(config, "MoonLit", "ClipSound"));
}

void MoonLitSettingsDialog::SaveValues()
{
	config_t *config = main_->Config();

	const std::string token = encoderCombo->currentData().toString().toStdString();
	config_set_string(config, "SimpleOutput", "RecEncoder", token.c_str());
	config_set_string(config, "SimpleOutput", "StreamEncoder", token.c_str());

	config_set_int(config, "SimpleOutput", "RecRBTime", replaySeconds->value());
	config_set_int(config, "SimpleOutput", "RecRBSize", replaySizeMb->value());

	uint32_t tracks = 0;
	if (trackMixed->isChecked())
		tracks |= 1u << 0;
	if (trackGame->isChecked())
		tracks |= 1u << 1;
	if (trackMic->isChecked())
		tracks |= 1u << 2;
	if (trackChat->isChecked())
		tracks |= 1u << 3;
	config_set_uint(config, "SimpleOutput", "RecTracks", tracks);

	const std::string path = outputPath->text().toStdString();
	if (!path.empty()) {
		config_set_string(config, "SimpleOutput", "FilePath", path.c_str());
	}

	const std::string mic = micDevice->text().trimmed().toStdString();
	if (!mic.empty()) {
		config_set_string(config, "MoonLit", "MicDeviceId", mic.c_str());
	}

	const std::string chat = chatExe->text().trimmed().toStdString();
	if (chat.empty()) {
		config_remove_value(config, "MoonLit", "ChatExe");
	} else {
		config_set_string(config, "MoonLit", "ChatExe", chat.c_str());
	}

	config_set_bool(config, "MoonLit", "ClipSound", clipSound->isChecked());
	config_save_safe(config, "tmp", nullptr);
	SetAutoStartEnabled(autoStart->isChecked());
	main_->ResetOutputs();
}

void MoonLitSettingsDialog::SaveAndAccept()
{
	SaveValues();
	accept();
}
