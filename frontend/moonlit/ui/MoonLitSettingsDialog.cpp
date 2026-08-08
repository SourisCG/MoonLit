#include "MoonLitSettingsDialog.hpp"

#include <moonlit/hotkeys/HotkeyManager.hpp>
#include <moonlit/output/EncoderResolver.hpp>
#include <moonlit/platform/IPlatformServices.hpp>

#include <settings/OBSHotkeyEdit.hpp>

#include <widgets/MoonLitStarfield.hpp>
#include <widgets/MoonLitTheme.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

#include <string>

#include <set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <mmdeviceapi.h>
#include <propkey.h>
#include <windows.h>
#pragma comment(lib, "ole32.lib")
#endif

namespace {

#ifdef _WIN32
/* PKEY_Device_FriendlyName defined inline to avoid the DEFINE_PROPERTYKEY
 * header conflict between propkey.h and functiondiscoverykeys_devpkey.h. */
static const PROPERTYKEY kDeviceFriendlyName = {
	{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

/* Windows Core Audio has no OBS-native input device list, so MoonLit
 * enumerates capture endpoints the same way win-wasapi does internally. */
QVector<std::pair<QString, QString>> AudioInputDevices()
{
	QVector<std::pair<QString, QString>> devices;
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	IMMDeviceEnumerator *enumerator = nullptr;
	if (CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
			     __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator)) != S_OK) {
		return devices;
	}

	IMMDeviceCollection *collection = nullptr;
	if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection))) {
		UINT count = 0;
		collection->GetCount(&count);
		for (UINT index = 0; index < count; ++index) {
			IMMDevice *device = nullptr;
			if (!SUCCEEDED(collection->Item(index, &device)))
				continue;

			LPWSTR id = nullptr;
			device->GetId(&id);
			IPropertyStore *store = nullptr;
			QString name;
			if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store))) {
				PROPVARIANT value;
				PropVariantInit(&value);
				if (SUCCEEDED(store->GetValue(kDeviceFriendlyName, &value)) &&
				    value.vt == VT_LPWSTR) {
					name = QString::fromWCharArray(value.pwszVal);
				}
				PropVariantClear(&value);
				store->Release();
			}
			devices.append({name, id ? QString::fromWCharArray(id) : QString()});
			CoTaskMemFree(id);
			device->Release();
		}
		collection->Release();
	}
	enumerator->Release();
	CoUninitialize();
	return devices;
}
#endif

/* Reflects the current endpoint state into the settings row without
 * re-resolving the device (used after setScalar/setMuted during dragging). */
void SyncVolumeRow(QLabel *label, QSlider *slider, QPushButton *mute,
		   const MoonLit::EndpointVolume *endpoint)
{
	slider->blockSignals(true);
	mute->blockSignals(true);
	const float scalar = endpoint->scalar();
	slider->setEnabled(endpoint->isOpen() && scalar >= 0.0f);
	slider->setValue(qRound(scalar * 100.0f));
	label->setText(slider->isEnabled() ? QStringLiteral("%1 %").arg(qRound(scalar * 100.0f))
					      : QStringLiteral("—"));
	mute->setEnabled(endpoint->isOpen());
	mute->setChecked(endpoint->muted());
	slider->blockSignals(false);
	mute->blockSignals(false);
}

/* Login startup is platform policy: HKCU Run on Windows, XDG autostart
 * desktop entry on Linux, behind the platform services abstraction. */
bool IsAutoStartEnabled()
{
	const auto platform = MoonLit::IPlatformServices::create();
	return platform && platform->isLoginStartupEnabled();
}

void SetAutoStartEnabled(bool enabled)
{
	const auto platform = MoonLit::IPlatformServices::create();
	if (platform) {
		platform->setLoginStartup(enabled);
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

/* A tab page for the settings dialog: a scroll area wrapping a form, so
 * small screens scroll inside the tab instead of the dialog growing past
 * the screen. The viewport stays transparent so the starfield shows. */
struct SettingsTab {
	QScrollArea *scroll = nullptr;
	QWidget *page = nullptr;
	QFormLayout *form = nullptr;
};

SettingsTab MakeSettingsTab(QWidget *parent)
{
	SettingsTab tab;
	tab.scroll = new QScrollArea(parent);
	tab.scroll->setWidgetResizable(true);
	tab.scroll->setFrameShape(QFrame::NoFrame);
	tab.scroll->viewport()->setAutoFillBackground(false);
	tab.page = new QWidget;
	tab.page->setObjectName(QStringLiteral("settingsPage"));
	tab.form = new QFormLayout(tab.page);
	tab.form->setContentsMargins(16, 12, 16, 12);
	tab.form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	tab.scroll->setWidget(tab.page);
	return tab;
}

} /* namespace */

MoonLitSettingsDialog::MoonLitSettingsDialog(OBSBasic *main, MoonLit::HotkeyManager *hotkeys,
					     QWidget *parent)
	: QDialog(parent), main_(main), hotkeys_(hotkeys)
{
	using namespace MoonLitTheme;
	setWindowTitle(QStringLiteral("Ajustes de MoonLit"));
	setModal(true);
	setObjectName(QStringLiteral("moonlitSettingsDialog"));
	setStyleSheet(QStringLiteral(
		"QDialog#moonlitSettingsDialog { background: #080303; }"
		"QLabel { color: %1; }"
		"QGroupBox { border: 1px solid %2; border-radius: 8px; margin-top: 10px;"
		" padding: 8px; color: %1; }"
		"QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %1; }"
		"QLineEdit, QSpinBox, QComboBox, QListWidget { background: %3; color: %1;"
		" border: 1px solid %2; border-radius: 7px; padding: 6px; }"
		"QComboBox::drop-down { border: 0; }"
		"QComboBox QAbstractItemView { background: %4; color: %1; selection-background-color: %5; }"
		"QListWidget::item { padding: 6px; border-bottom: 1px solid %2; }"
		"QListWidget::item:selected { background: %4; color: %1; }"
		"QPushButton { min-height: 30px; padding: 0 12px; border: 1px solid %2;"
		" border-radius: 7px; background: %3; color: %1; }"
		"QPushButton:hover { background: %4; border-color: %6; }"
		"QPushButton:pressed { background: %7; }"
		"QPushButton:disabled { color: %8; background: %3; }"
		"QCheckBox { color: %1; }"
		"QSlider::groove:horizontal { height: 4px; background: %2; border-radius: 2px; }"
		"QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px;"
		" background: %6; }"
		"QTabWidget::pane { border: 1px solid %2; border-radius: 8px; top: -1px; background: transparent; }"
		"QTabBar::tab { background: transparent; color: %8; padding: 8px 16px;"
		" border: 0; border-top-left-radius: 8px; border-top-right-radius: 8px;"
		" margin-right: 2px; font-weight: 500; }"
		"QTabBar::tab:hover { color: %1; background: %4; }"
		"QTabBar::tab:selected { color: %1; background: %3; border-bottom: 2px solid %6; }"
		"QScrollArea { background: transparent; border: none; }"
		"QWidget#settingsPage { background: transparent; }"
		"QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
		"QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 30px; }"
		"QScrollBar::handle:vertical:hover { background: %6; }"
		"QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }"
		"QScrollBar::handle:horizontal { background: %4; border-radius: 4px; min-width: 30px; }"
		"QScrollBar::handle:horizontal:hover { background: %6; }"
		"QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
		"QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }")
				.arg(css(text()), css(border()), css(bgSurface()), css(bgElevated()),
				     css(accent()), css(accentHover()), css(night()), css(textMuted())));

	/* Night-sky background behind the form. It paints only the stars: the
	 * dialog itself paints the asphalt sky, so the starfield's animation
	 * repaints never wipe out the settings widgets above it. */
	starfield_ = new MoonLitStarfield(this);
	starfield_->setPaintBackground(false);
	starfield_->setAttribute(Qt::WA_TransparentForMouseEvents);
	starfield_->lower();

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

	/* The four recording quality levels of OBS simple mode, with the same
	 * config values SimpleOutput consumes (Stream/Small/HQ/Lossless). */
	qualityCombo = new QComboBox(this);
	qualityCombo->addItem(QStringLiteral("Igual que la retransmisión"), QStringLiteral("Stream"));
	qualityCombo->addItem(QStringLiteral("Pequeño"), QStringLiteral("Small"));
	qualityCombo->addItem(QStringLiteral("Alta"), QStringLiteral("HQ"));
	qualityCombo->addItem(QStringLiteral("Sin pérdida (utvideo AVI)"), QStringLiteral("Lossless"));

	presetCombo = new QComboBox(this);
	connect(encoderCombo, &QComboBox::currentIndexChanged, this,
		[this](int) { PopulatePresetCombo(); });
	connect(qualityCombo, &QComboBox::currentIndexChanged, this,
		[this](int) { PopulatePresetCombo(); });
	PopulatePresetCombo();

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

	micDevice = new QComboBox(this);
	micDevice->addItem(QStringLiteral("Predeterminado"), QStringLiteral("default"));
	for (const auto &[name, id] : AudioInputDevices()) {
		micDevice->addItem(name, id);
	}

	desktopDevice = new QComboBox(this);
	desktopDevice->addItem(QStringLiteral("Predeterminado"), QStringLiteral("default"));
	auto enumMonitoring = [](void *param, const char *name, const char *id) {
		auto *combo = static_cast<QComboBox *>(param);
		combo->addItem(QString::fromUtf8(name), QString::fromUtf8(id));
		return true;
	};
	obs_enum_audio_monitoring_devices(enumMonitoring, desktopDevice);

	/* Physical device volume rows: the slider acts on the Windows endpoint
	 * (IAudioEndpointVolume) of the selected device, immediately, because it
	 * is the volume the user hears. Recording levels are separate (mixer). */
	micVolumeSlider = new QSlider(Qt::Horizontal, this);
	micVolumeSlider->setRange(0, 100);
	micVolumeLabel = new QLabel(this);
	micVolumeLabel->setFixedWidth(40);
	micMute = new QPushButton(QStringLiteral("M"), this);
	micMute->setCheckable(true);
	micMute->setFixedWidth(36);
	micMute->setToolTip(QStringLiteral("Silenciar dispositivo"));

	desktopVolumeSlider = new QSlider(Qt::Horizontal, this);
	desktopVolumeSlider->setRange(0, 100);
	desktopVolumeLabel = new QLabel(this);
	desktopVolumeLabel->setFixedWidth(40);
	desktopMute = new QPushButton(QStringLiteral("M"), this);
	desktopMute->setCheckable(true);
	desktopMute->setFixedWidth(36);
	desktopMute->setToolTip(QStringLiteral("Silenciar dispositivo"));

	connect(micVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
		micEndpoint_.setScalar(static_cast<float>(value) / 100.0f);
		SyncVolumeRow(micVolumeLabel, micVolumeSlider, micMute, &micEndpoint_);
	});
	connect(micMute, &QPushButton::toggled, this, [this](bool checked) {
		micEndpoint_.setMuted(checked);
		SyncVolumeRow(micVolumeLabel, micVolumeSlider, micMute, &micEndpoint_);
	});
	connect(micDevice, &QComboBox::currentIndexChanged, this, [this] {
		PopulateVolumeRow(micVolumeLabel, micVolumeSlider, micMute, &micEndpoint_,
				  MoonLit::EndpointVolume::Direction::Capture,
				  micDevice->currentData().toString());
	});

	connect(desktopVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
		desktopEndpoint_.setScalar(static_cast<float>(value) / 100.0f);
		SyncVolumeRow(desktopVolumeLabel, desktopVolumeSlider, desktopMute, &desktopEndpoint_);
	});
	connect(desktopMute, &QPushButton::toggled, this, [this](bool checked) {
		desktopEndpoint_.setMuted(checked);
		SyncVolumeRow(desktopVolumeLabel, desktopVolumeSlider, desktopMute, &desktopEndpoint_);
	});
	connect(desktopDevice, &QComboBox::currentIndexChanged, this, [this] {
		PopulateVolumeRow(desktopVolumeLabel, desktopVolumeSlider, desktopMute, &desktopEndpoint_,
				  MoonLit::EndpointVolume::Direction::Render,
				  desktopDevice->currentData().toString());
	});

	chatExe = new QLineEdit(this);
	chatExe->setPlaceholderText(QStringLiteral("Discord.exe"));

	autoStart = new QCheckBox(QStringLiteral("Iniciar MoonLit con Windows (oculto en la bandeja)"), this);

	minimizeToTray = new QCheckBox(
		QStringLiteral("Minimizar a la bandeja: la X deja la app en segundo plano"), this);

	clipSound = new QCheckBox(QStringLiteral("Sonido al guardar clip"), this);

	noiseSuppression = new QCheckBox(QStringLiteral("Supresion de ruido (tipo Krisp)"), this);

	/* Captures a key combination with optional modifiers: click the field
	 * and press e.g. Ctrl+F8. "Restablecer F8" restores the default. */
	saveClipHotkeyEdit = new OBSHotkeyEdit(this);
	saveClipHotkeyEdit->setPlaceholderText(QStringLiteral("Pulsa una combinación…"));
	saveClipHotkeyEdit->setToolTip(QStringLiteral("Haz clic y pulsa la combinación (ej. Ctrl+F8)"));
	resetHotkeyButton = new QPushButton(QStringLiteral("Restablecer F8"), this);
	resetHotkeyButton->setToolTip(QStringLiteral("Vuelve a F8"));
	connect(resetHotkeyButton, &QPushButton::clicked, this, [this]() {
		saveClipHotkeyEdit->original = {0, OBS_KEY_F8};
		saveClipHotkeyEdit->ResetKey();
	});

	QLabel *hotkeyNote = new QLabel(
		QStringLiteral("Combinación global para guardar el clip, también con el juego en primer plano "
			       "(puede incluir Ctrl, Alt, Shift o Win)."),
		this);
	hotkeyNote->setWordWrap(true);

	QLabel *formatNote = new QLabel(
		QStringLiteral("Formato de grabación: MKV (autoritativo; MP4 solo como exportación)."), this);
	formatNote->setWordWrap(true);

	QLabel *audioNote = new QLabel(
		QStringLiteral("Pistas: 1 mezcla (audio de escritorio de la escena), 2 juego, 3 micrófono, 4 chat."),
		this);
	audioNote->setWordWrap(true);

	QLabel *folderNote = new QLabel(
		QStringLiteral("Por defecto MoonLit usa su propia carpeta (MoonLit/Clips) para evitar el bloqueo del "
			       "Acceso controlado a carpetas de Windows; podés cambiarla aquí cuando quieras."),
		this);
	folderNote->setWordWrap(true);

	QLabel *losslessNote = new QLabel(
		QStringLiteral("Con 'Sin pérdida' se graba AVI utvideo y el guardado de clips queda desactivado."),
		this);
	losslessNote->setWordWrap(true);

	QLabel *deviceVolumeNote = new QLabel(
		QStringLiteral("El volumen de entrada/salida ajusta el dispositivo real de Windows (lo que se oye), "
			       "no la grabación. Los niveles por pista se ajustan en el Mezclador."),
		this);
	deviceVolumeNote->setWordWrap(true);

	gameListWidget = new QListWidget(this);
	gameListWidget->setFixedHeight(110);
	removeGameButton = new QPushButton(QStringLiteral("Quitar seleccionado"), this);
	removeGameButton->setEnabled(false);
	connect(gameListWidget, &QListWidget::itemSelectionChanged, this, [this]() {
		removeGameButton->setEnabled(gameListWidget->currentRow() >= 0);
	});
	connect(removeGameButton, &QPushButton::clicked, this, [this]() {
		delete gameListWidget->takeItem(gameListWidget->currentRow());
	});
	QLabel *gameListNote = new QLabel(
		QStringLiteral("Juegos recordados tras seleccionarlos manualmente; se detectan solos "
			       "cuando están en primer plano."),
		this);
	gameListNote->setWordWrap(true);

	QGroupBox *aboutGroup = new QGroupBox(QStringLiteral("Acerca de MoonLit"), this);
	QLabel *aboutText = new QLabel(
		QStringLiteral("MoonLit 0.1.2\nGrabadora de clips local basada en OBS Studio 32.2.1.\n"
			       "Software libre bajo GPLv2."),
		aboutGroup);
	aboutText->setWordWrap(true);
	QVBoxLayout *aboutLayout = new QVBoxLayout(aboutGroup);
	aboutLayout->addWidget(aboutText);

	QHBoxLayout *pathLayout = new QHBoxLayout;
	pathLayout->addWidget(outputPath, 1);
	pathLayout->addWidget(browse);

	/* The four audio track toggles fit in one row. */
	auto *tracksRow = new QHBoxLayout;
	tracksRow->setSpacing(14);
	tracksRow->addWidget(trackMixed);
	tracksRow->addWidget(trackGame);
	tracksRow->addWidget(trackMic);
	tracksRow->addWidget(trackChat);
	tracksRow->addStretch(1);

	/* Responsive layout: settings are grouped in tabs, and every tab is a
	 * scroll area, so the dialog keeps a fixed window size and small
	 * screens scroll inside the tab instead of growing past the screen. */
	QTabWidget *tabs = new QTabWidget(this);
	tabs->setObjectName(QStringLiteral("moonlitSettingsTabs"));

	SettingsTab recording = MakeSettingsTab(tabs);
	recording.form->addRow(QStringLiteral("Encoder de vídeo:"), encoderCombo);
	recording.form->addRow(QStringLiteral("Calidad de grabación:"), qualityCombo);
	recording.form->addRow(QStringLiteral("Preset:"), presetCombo);
	recording.form->addRow(losslessNote);
	recording.form->addRow(QStringLiteral("Duración del buffer:"), replaySeconds);
	recording.form->addRow(QStringLiteral("Tamaño máximo:"), replaySizeMb);
	recording.form->addRow(QStringLiteral("Pistas de audio:"), tracksRow);
	recording.form->addRow(QStringLiteral("Carpeta de grabación:"), pathLayout);
	recording.form->addRow(folderNote);
	recording.form->addRow(audioNote);
	recording.form->addRow(formatNote);
	tabs->addTab(recording.scroll, QStringLiteral("Grabación"));

	SettingsTab audio = MakeSettingsTab(tabs);
	audio.form->addRow(QStringLiteral("Microfono (entrada):"), micDevice);
	auto *micVolumeRow = new QHBoxLayout;
	micVolumeRow->addWidget(micVolumeSlider, 1);
	micVolumeRow->addWidget(micVolumeLabel);
	micVolumeRow->addWidget(micMute);
	audio.form->addRow(QStringLiteral("Volumen del microfono:"), micVolumeRow);
	audio.form->addRow(QStringLiteral("Audio de escritorio (salida):"), desktopDevice);
	auto *desktopVolumeRow = new QHBoxLayout;
	desktopVolumeRow->addWidget(desktopVolumeSlider, 1);
	desktopVolumeRow->addWidget(desktopVolumeLabel);
	desktopVolumeRow->addWidget(desktopMute);
	audio.form->addRow(QStringLiteral("Volumen de salida:"), desktopVolumeRow);
	audio.form->addRow(deviceVolumeNote);
	audio.form->addRow(QStringLiteral("Chat (ejecutable):"), chatExe);
	tabs->addTab(audio.scroll, QStringLiteral("Audio"));

	SettingsTab games = MakeSettingsTab(tabs);
	games.form->addRow(QStringLiteral("Juegos recordados:"), gameListWidget);
	games.form->addRow(removeGameButton);
	games.form->addRow(gameListNote);
	tabs->addTab(games.scroll, QStringLiteral("Juegos"));

	SettingsTab general = MakeSettingsTab(tabs);
	general.form->addRow(autoStart);
	general.form->addRow(minimizeToTray);
	general.form->addRow(clipSound);
	general.form->addRow(noiseSuppression);
	auto *hotkeyRow = new QHBoxLayout;
	hotkeyRow->addWidget(saveClipHotkeyEdit, 1);
	hotkeyRow->addWidget(resetHotkeyButton);
	general.form->addRow(QStringLiteral("Tecla para guardar clip:"), hotkeyRow);
	general.form->addRow(hotkeyNote);
	general.form->addRow(aboutGroup);
	tabs->addTab(general.scroll, QStringLiteral("General"));

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &MoonLitSettingsDialog::SaveAndAccept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);
	layout->addWidget(tabs, 1);
	layout->addWidget(buttons);

	resize(620, 700);
	setMinimumSize(480, 520);

	LoadCurrentValues();
}

void MoonLitSettingsDialog::resizeEvent(QResizeEvent *event)
{
	QDialog::resizeEvent(event);
	if (starfield_) {
		starfield_->setGeometry(rect());
	}
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

	const char *savedQuality = config_get_string(config, "SimpleOutput", "RecQuality");
	const int qualityIndex = qualityCombo->findData(QString::fromUtf8(savedQuality ? savedQuality : "Stream"));
	if (qualityIndex >= 0) {
		qualityCombo->setCurrentIndex(qualityIndex);
	}

	/* Reads the saved preset for the current encoder (and its key). */
	PopulatePresetCombo();

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
	const int micIndex = micDevice->findData(QString::fromUtf8(mic ? mic : "default"));
	micDevice->setCurrentIndex(micIndex >= 0 ? micIndex : 0);

	const char *desktop = config_get_string(config, "MoonLit", "DesktopDeviceId");
	const int desktopIndex = desktopDevice->findData(QString::fromUtf8(desktop ? desktop : "default"));
	desktopDevice->setCurrentIndex(desktopIndex >= 0 ? desktopIndex : 0);

	const char *chat = config_get_string(config, "MoonLit", "ChatExe");
	chatExe->setText(QString::fromUtf8(chat ? chat : ""));

	autoStart->setChecked(IsAutoStartEnabled());

	minimizeToTray->setChecked(config_get_bool(App()->GetUserConfig(), "BasicWindow", "SysTrayMinimizeToTray"));

	clipSound->setChecked(config_get_bool(config, "MoonLit", "ClipSound"));
	noiseSuppression->setChecked(config_get_bool(config, "MoonLit", "NoiseSuppression"));

	if (hotkeys_) {
		saveClipHotkeyEdit->original = hotkeys_->saveClipHotkey();
		saveClipHotkeyEdit->ResetKey();
	}

	gameListWidget->clear();
	const char *gameList = config_get_string(config, "MoonLit", "GameList");
	if (gameList && *gameList) {
		const QStringList entries = QString::fromUtf8(gameList).split(QChar('\n'), Qt::SkipEmptyParts);
		for (const QString &entry : entries) {
			gameListWidget->addItem(entry);
		}
	}

	PopulateVolumeRow(micVolumeLabel, micVolumeSlider, micMute, &micEndpoint_,
			  MoonLit::EndpointVolume::Direction::Capture, micDevice->currentData().toString());
	PopulateVolumeRow(desktopVolumeLabel, desktopVolumeSlider, desktopMute, &desktopEndpoint_,
			  MoonLit::EndpointVolume::Direction::Render,
			  desktopDevice->currentData().toString());
}

void MoonLitSettingsDialog::PopulatePresetCombo()
{
	const QString token = encoderCombo->currentData().toString();
	const QString quality = qualityCombo->currentData().toString();

	presetCombo->blockSignals(true);
	presetCombo->clear();

	auto addPreset = [this](const char *name, const char *value) {
		presetCombo->addItem(QString::fromUtf8(name), QString::fromUtf8(value));
	};

	bool hasPresets = true;
	if (token == QStringLiteral("qsv") || token == QStringLiteral("qsv_av1")) {
		addPreset("speed", "speed");
		addPreset("balanced", "balanced");
		addPreset("quality", "quality");
	} else if (token == QStringLiteral("amd") || token == QStringLiteral("amd_hevc")) {
		addPreset("Speed", "speed");
		addPreset("Balanced", "balanced");
		addPreset("Quality", "quality");
	} else if (token == QStringLiteral("amd_av1")) {
		addPreset("Speed", "speed");
		addPreset("Balanced", "balanced");
		addPreset("Quality", "quality");
		addPreset("High Quality", "highQuality");
	} else if (token == QStringLiteral("nvenc") || token == QStringLiteral("nvenc_hevc") ||
		   token == QStringLiteral("nvenc_av1")) {
		/* NVENC exposes its real preset list through the encoder
		 * properties ("preset2" for the legacy ffmpeg wrapper). */
		const std::string id = MoonLit::EncoderResolver::SimpleTokenToEncoderId(token.toStdString());
		OBSProperties props = obs_get_encoder_properties(id.c_str());
		const bool ffmpeg = id.rfind("ffmpeg_", 0) == 0;
		obs_property_t *p = obs_properties_get(props, ffmpeg ? "preset2" : "preset");
		hasPresets = p && obs_property_list_item_count(p) > 0;
		if (hasPresets) {
			const size_t num = obs_property_list_item_count(p);
			for (size_t i = 0; i < num; ++i) {
				addPreset(obs_property_list_item_name(p, i), obs_property_list_item_string(p, i));
			}
		}
	} else if (token == QStringLiteral("x264")) {
		addPreset("ultrafast", "ultrafast");
		addPreset("superfast", "superfast");
		addPreset("veryfast", "veryfast");
		addPreset("faster", "faster");
		addPreset("fast", "fast");
	} else {
		/* Raw obs ids (ffmpeg svt/aom, ...) enumerate their preset
		 * property when they expose one; otherwise no presets. */
		OBSProperties props = obs_get_encoder_properties(token.toUtf8().constData());
		obs_property_t *p = obs_properties_get(props, "preset");
		hasPresets = p && obs_property_list_item_count(p) > 0;
		if (hasPresets) {
			const size_t num = obs_property_list_item_count(p);
			for (size_t i = 0; i < num; ++i) {
				addPreset(obs_property_list_item_name(p, i), obs_property_list_item_string(p, i));
			}
		}
	}

	/* Stream reuses the streaming settings and Lossless records utvideo;
	 * neither one uses per-codec presets. */
	const bool qualityUsesPresets = quality != QStringLiteral("Stream") &&
					quality != QStringLiteral("Lossless");
	presetCombo->setVisible(qualityUsesPresets && hasPresets);

	if (hasPresets) {
		const char *presetKey = MoonLit::EncoderResolver::SimpleTokenToPresetKey(token.toStdString());
		const char *saved = config_get_string(main_->Config(), "SimpleOutput", presetKey);
		int index = saved ? presetCombo->findData(QString::fromUtf8(saved)) : -1;
		if (index < 0) {
			index = 0;
		}
		presetCombo->setCurrentIndex(index);
	}
	presetCombo->blockSignals(false);
}

void MoonLitSettingsDialog::PopulateVolumeRow(QLabel *label, QSlider *slider, QPushButton *mute,
					      MoonLit::EndpointVolume *endpoint,
					      MoonLit::EndpointVolume::Direction direction,
					      const QString &deviceId)
{
	endpoint->open(direction, deviceId);
	SyncVolumeRow(label, slider, mute, endpoint);
}

void MoonLitSettingsDialog::SaveValues()
{
	config_t *config = main_->Config();

	const std::string token = encoderCombo->currentData().toString().toStdString();
	config_set_string(config, "SimpleOutput", "RecEncoder", token.c_str());
	config_set_string(config, "SimpleOutput", "StreamEncoder", token.c_str());

	const std::string quality = qualityCombo->currentData().toString().toStdString();
	config_set_string(config, "SimpleOutput", "RecQuality", quality.c_str());

	/* Presets apply to per-codec quality levels; Stream and Lossless ignore
	 * them, so leave the saved value alone when they are selected. */
	if (quality != "Stream" && quality != "Lossless" && presetCombo->count() > 0) {
		const char *presetKey = MoonLit::EncoderResolver::SimpleTokenToPresetKey(token);
		const std::string preset = presetCombo->currentData().toString().toStdString();
		config_set_string(config, "SimpleOutput", presetKey, preset.c_str());
	}

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

	const std::string mic = micDevice->currentData().toString().toStdString();
	if (mic.empty()) {
		config_remove_value(config, "MoonLit", "MicDeviceId");
	} else {
		config_set_string(config, "MoonLit", "MicDeviceId", mic.c_str());
	}

	const std::string desktop = desktopDevice->currentData().toString().toStdString();
	if (desktop.empty() || desktop == "default") {
		config_remove_value(config, "MoonLit", "DesktopDeviceId");
	} else {
		config_set_string(config, "MoonLit", "DesktopDeviceId", desktop.c_str());
	}

	const std::string chat = chatExe->text().trimmed().toStdString();
	if (chat.empty()) {
		config_remove_value(config, "MoonLit", "ChatExe");
	} else {
		config_set_string(config, "MoonLit", "ChatExe", chat.c_str());
	}

	config_set_bool(config, "MoonLit", "ClipSound", clipSound->isChecked());
	config_set_bool(config, "MoonLit", "NoiseSuppression", noiseSuppression->isChecked());
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "SysTrayMinimizeToTray", minimizeToTray->isChecked());

	if (hotkeys_) {
		hotkeys_->setSaveClipHotkey(config, saveClipHotkeyEdit->key);
	}

	QStringList gameList;
	for (int row = 0; row < gameListWidget->count(); ++row) {
		const QString entry = gameListWidget->item(row)->text().trimmed();
		if (!entry.isEmpty()) {
			gameList.append(entry);
		}
	}
	if (gameList.isEmpty()) {
		config_remove_value(config, "MoonLit", "GameList");
	} else {
		config_set_string(config, "MoonLit", "GameList", gameList.join(QChar('\n')).toUtf8().constData());
	}

	config_save_safe(config, "tmp", nullptr);
	SetAutoStartEnabled(autoStart->isChecked());
	main_->ResetOutputs();
	main_->ApplyMoonLitNoiseSuppression();
}

void MoonLitSettingsDialog::SaveAndAccept()
{
	SaveValues();
	accept();
}
