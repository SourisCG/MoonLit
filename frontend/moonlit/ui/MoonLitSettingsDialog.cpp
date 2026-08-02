#include "MoonLitSettingsDialog.hpp"

#include <moonlit/output/EncoderResolver.hpp>

#include <widgets/OBSBasic.hpp>

#include <obs.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <string>

namespace {

bool VideoEncoderRegistered(const char *id)
{
	const char *val;
	size_t i = 0;

	while (obs_enum_encoder_types(i++, &val)) {
		if (strcmp(val, id) == 0) {
			return true;
		}
	}

	return false;
}

/* Maps an obs encoder id back to the simple output token used in config. */
std::string EncoderIdToToken(const std::string &id)
{
	if (id == "obs_nvenc_h264_tex" || id == "ffmpeg_nvenc" || id == "obs_nvenc")
		return "nvenc";
	if (id == "obs_qsv11_v2" || id == "obs_qsv11")
		return "qsv";
	if (id == "h264_texture_amf" || id == "ffmpeg_amf_h264")
		return "amd";
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

	for (const std::string &id : MoonLit::EncoderResolver::FallbackChain()) {
		if (!VideoEncoderRegistered(id.c_str()))
			continue;

		const char *display = obs_encoder_get_display_name(id.c_str());
		std::string token = EncoderIdToToken(id);
		encoderCombo->addItem(QString::fromUtf8(display ? display : id.c_str()),
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

	QLabel *formatNote = new QLabel(
		QStringLiteral("Formato de grabación: MKV (autoritativo; MP4 solo como exportación)."), this);
	formatNote->setWordWrap(true);

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
	form->addRow(formatNote);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &MoonLitSettingsDialog::SaveAndAccept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addLayout(form);
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

	config_save_safe(config, "tmp", nullptr);
	main_->ResetOutputs();
}

void MoonLitSettingsDialog::SaveAndAccept()
{
	SaveValues();
	accept();
}
