#include "MoonLitLibraryWidget.hpp"

#include "ClipFrameStrip.hpp"
#include "MoonLitTheme.hpp"
#include "MoonLitTimelineEditor.hpp"

#include <obs.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

/* Pick a destination inside clipsDir that does not collide with an existing
 * file, so an import never overwrites a clip that is already in the library. */
QString uniqueClipDestination(const QDir &clipsDir, const QString &fileName)
{
	QString baseName = QFileInfo(fileName).completeBaseName();
	const QString extension = QFileInfo(fileName).suffix();

	QString candidate = baseName + QLatin1Char('.') + extension;
	int counter = 2;
	while (QFileInfo::exists(clipsDir.filePath(candidate))) {
		candidate = QStringLiteral("%1 (%2).%3").arg(baseName).arg(counter).arg(extension);
		++counter;
	}
	return clipsDir.filePath(candidate);
}

} // namespace

MoonLitLibraryWidget::MoonLitLibraryWidget(QWidget *parent) : QWidget(parent)
{
	using namespace MoonLitTheme;
	setObjectName(QStringLiteral("moonlitLibrary"));
	setStyleSheet(QStringLiteral(R"(
        #moonlitLibrary { background: %1; color: %2; }
        QLabel#libraryTitle { color: #ffffff; font-size: 24px; font-weight: 700; }
        QLabel#libraryDetails, QLabel#libraryStatus { color: %3; }
        QLineEdit, QListWidget, QSpinBox, QComboBox { background: %4; color: %2; border: 1px solid %5; border-radius: 7px; padding: 7px; }
        QComboBox::drop-down { border: 0; }
        QComboBox QAbstractItemView { background: %6; color: %2; selection-background-color: %7; }
        QListWidget::item { padding: 8px; border-bottom: 1px solid %8; }
        QListWidget::item:selected { background: %9; }
        QPushButton { min-height: 34px; padding: 0 12px; border: 1px solid %5; border-radius: 7px; background: %4; color: %2; font-weight: 500; }
        QPushButton:hover { background: %9; border-color: %7; }
        QPushButton:pressed { background: %10; }
        QPushButton:disabled { color: %3; background: %11; border-color: %12; }
    )")
			.arg(css(bgDeep()), css(text()), css(textMuted()), css(bgSurface()), css(border()),
			     QColor(0x2f, 0x31, 0x42).name(), css(accent()), QColor(0x3a, 0x3d, 0x4d).name(),
			     css(bgElevated()), QColor(0x2f, 0x31, 0x42).name(), QColor(0x24, 0x25, 0x2f).name(),
			     QColor(0x3a, 0x3d, 0x4d).name()));

	searchDebounceTimer_ = new QTimer(this);
	searchDebounceTimer_->setSingleShot(true);
	searchDebounceTimer_->setInterval(300);
	connect(searchDebounceTimer_, &QTimer::timeout, this, &MoonLitLibraryWidget::refresh);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(28, 24, 28, 24);
	root->setSpacing(12);

	auto *header = new QHBoxLayout();
	auto *backButton = new QPushButton(QStringLiteral("Volver"), this);
	auto *title = new QLabel(QStringLiteral("Biblioteca"), this);
	title->setObjectName(QStringLiteral("libraryTitle"));
	auto *refreshButton = new QPushButton(QStringLiteral("Actualizar"), this);
	auto *importButton = new QPushButton(QStringLiteral("Importar"), this);
	auto *timelineButton = new QPushButton(QStringLiteral("Timeline"), this);
	header->addWidget(backButton);
	header->addWidget(title);
	header->addStretch(1);
	header->addWidget(timelineButton);
	header->addWidget(importButton);
	header->addWidget(refreshButton);
	root->addLayout(header);

	/* Library page and timeline editor share the space below the header. */
	stack_ = new QStackedWidget(this);
	libraryPage_ = new QWidget(stack_);
	auto *libraryLayout = new QVBoxLayout(libraryPage_);
	libraryLayout->setContentsMargins(0, 0, 0, 0);
	libraryLayout->setSpacing(12);

	searchEdit_ = new QLineEdit(this);
	searchEdit_->setPlaceholderText(QStringLiteral("Buscar clips, juegos o archivos..."));
	filterCombo_ = new QComboBox(this);
	filterCombo_->addItems({QStringLiteral("Todos"), QStringLiteral("Disponibles"),
				QStringLiteral("Faltantes")});
	filterCombo_->setCurrentIndex(0);
	auto *searchRow = new QHBoxLayout();
	searchRow->addWidget(searchEdit_, 1);
	searchRow->addWidget(filterCombo_);
	libraryLayout->addLayout(searchRow);

	auto *content = new QHBoxLayout();
	gridScroll_ = new QScrollArea(libraryPage_);
	gridScroll_->setWidgetResizable(true);
	gridScroll_->setFrameShape(QFrame::NoFrame);
	gridContainer_ = new QWidget(gridScroll_);
	gridLayout_ = new QGridLayout(gridContainer_);
	gridLayout_->setContentsMargins(4, 4, 4, 4);
	gridLayout_->setSpacing(10);
	gridLayout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	gridScroll_->setWidget(gridContainer_);
	gridScroll_->setMinimumWidth(460);
	content->addWidget(gridScroll_, 2);

	auto *detailsContainer = new QWidget(libraryPage_);
	auto *details = new QVBoxLayout(detailsContainer);
	detailsLabel_ = new QLabel(QStringLiteral("Selecciona un clip"), this);
	detailsLabel_->setObjectName(QStringLiteral("libraryDetails"));
	detailsLabel_->setWordWrap(true);
	details->addWidget(detailsLabel_);

	openButton_ = new QPushButton(QStringLiteral("Abrir"), this);
	revealButton_ = new QPushButton(QStringLiteral("Mostrar en carpeta"), this);
	removeButton_ = new QPushButton(QStringLiteral("Enviar a papelera"), this);
	openButton_->setEnabled(false);
	revealButton_->setEnabled(false);
	removeButton_->setEnabled(false);
	details->addWidget(openButton_);
	details->addWidget(revealButton_);
	details->addWidget(removeButton_);

	previewImage_ = new QLabel(this);
	previewImage_->setObjectName(QStringLiteral("libraryPreview"));
	previewImage_->setFixedHeight(90);
	previewImage_->setAlignment(Qt::AlignCenter);
	previewImage_->setStyleSheet(QStringLiteral("background: #000000; border: 1px solid %1;")
						 .arg(MoonLitTheme::css(MoonLitTheme::border())));
	previewImage_->setText(QStringLiteral("Vista previa"));
	details->addWidget(previewImage_);

	frameStrip_ = new ClipFrameStrip(this);
	details->addWidget(frameStrip_);

	auto *trimTitle = new QLabel(QStringLiteral("Exportar recorte rapido"), this);
	details->addWidget(trimTitle);
	auto *trimRow = new QHBoxLayout();
	startSeconds_ = new QSpinBox(this);
	startSeconds_->setRange(0, 86400);
	startSeconds_->setSuffix(QStringLiteral(" s inicio"));
	endSeconds_ = new QSpinBox(this);
	endSeconds_->setRange(0, 86400);
	endSeconds_->setSuffix(QStringLiteral(" s fin"));
	trimRow->addWidget(startSeconds_);
	trimRow->addWidget(endSeconds_);
	details->addLayout(trimRow);

	auto *audioRow = new QHBoxLayout();
	muteCheck_ = new QCheckBox(QStringLiteral("Silenciar"), this);
	gainSlider_ = new QSlider(Qt::Horizontal, this);
	gainSlider_->setRange(-20, 20);
	gainSlider_->setValue(0);
	gainValue_ = new QLabel(QStringLiteral("0 dB"), this);
	gainValue_->setMinimumWidth(48);
	audioRow->addWidget(muteCheck_);
	audioRow->addWidget(gainSlider_, 1);
	audioRow->addWidget(gainValue_);
	details->addLayout(audioRow);

	saveEditsButton_ = new QPushButton(QStringLiteral("Guardar edicion"), this);
	saveEditsButton_->setEnabled(false);
	details->addWidget(saveEditsButton_);

	exportButton_ = new QPushButton(QStringLiteral("Exportar MP4"), this);
	exportButton_->setEnabled(false);
	details->addWidget(exportButton_);
	cancelButton_ = new QPushButton(QStringLiteral("Cancelar"), this);
	cancelButton_->setEnabled(false);
	details->addWidget(cancelButton_);
	details->addStretch(1);

	auto *detailsScroll = new QScrollArea(libraryPage_);
	detailsScroll->setWidgetResizable(true);
	detailsScroll->setFrameShape(QFrame::NoFrame);
	detailsScroll->setWidget(detailsContainer);
	content->addWidget(detailsScroll, 1);
	libraryLayout->addLayout(content, 1);
	stack_->addWidget(libraryPage_);

	timelineEditor_ = new MoonLitTimelineEditor(stack_);
	stack_->addWidget(timelineEditor_);
	root->addWidget(stack_, 1);

	statusLabel_ = new QLabel(this);
	statusLabel_->setObjectName(QStringLiteral("libraryStatus"));
	statusLabel_->setWordWrap(true);
	root->addWidget(statusLabel_);

	connect(backButton, &QPushButton::clicked, this, &MoonLitLibraryWidget::backRequested);
	connect(refreshButton, &QPushButton::clicked, this, &MoonLitLibraryWidget::refresh);
	connect(importButton, &QPushButton::clicked, this, &MoonLitLibraryWidget::importFiles);
	connect(timelineButton, &QPushButton::clicked, this, &MoonLitLibraryWidget::openTimelineEditor);
	connect(filterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&MoonLitLibraryWidget::onFilterChanged);
	connect(searchEdit_, &QLineEdit::textChanged, this, [this]() { searchDebounceTimer_->start(); });
	connect(openButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::openSelected);
	connect(revealButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::revealSelected);
	connect(removeButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::removeSelected);
	connect(exportButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::exportSelected);
	connect(cancelButton_, &QPushButton::clicked, this, [this]() {
		setStatus(QStringLiteral("Cancelando..."));
		if (queue_) {
			queue_->cancelCurrent();
		}
	});
	connect(saveEditsButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::saveEdits);
	connect(startSeconds_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
		if (frameStrip_ && previewDurationMs_ > 0) {
			frameStrip_->setTrim(static_cast<qint64>(value) * 1000,
					     static_cast<qint64>(endSeconds_->value()) * 1000);
		}
	});
	connect(endSeconds_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
		if (frameStrip_ && previewDurationMs_ > 0) {
			frameStrip_->setTrim(static_cast<qint64>(startSeconds_->value()) * 1000,
					     static_cast<qint64>(value) * 1000);
		}
	});
	connect(frameStrip_, &ClipFrameStrip::seekRequested, this, [this](qint64 positionMs) {
		if (previewPath_.isEmpty())
			return;
		QMetaObject::invokeMethod(jobs_,
					  [this, path = previewPath_, positionMs]() {
						  jobs_->previewFrameAt(path, positionMs);
					  },
					  Qt::QueuedConnection);
	});
	connect(frameStrip_, &ClipFrameStrip::trimChanged, this, [this](qint64 startMs, qint64 endMs) {
		QSignalBlocker blockerStart(startSeconds_);
		QSignalBlocker blockerEnd(endSeconds_);
		startSeconds_->setValue(static_cast<int>(startMs / 1000));
		endSeconds_->setValue(endMs >= 0 ? static_cast<int>(endMs / 1000)
						 : static_cast<int>(previewDurationMs_ / 1000));
	});
	connect(gainSlider_, &QSlider::valueChanged, this,
		[this](int value) { gainValue_->setText(QStringLiteral("%1 dB").arg(value)); });
	connect(muteCheck_, &QCheckBox::toggled, this,
		[this](bool checked) { gainSlider_->setEnabled(!checked); });

	workerThread_ = new QThread(this);
	jobs_ = new MoonLit::ClipJobs(paths_, &repository_);
	jobs_->moveToThread(workerThread_);
	connect(workerThread_, &QThread::finished, jobs_, &QObject::deleteLater);
	connect(jobs_, &MoonLit::ClipJobs::libraryLoaded, this, &MoonLitLibraryWidget::onLibraryLoaded);
	connect(jobs_, &MoonLit::ClipJobs::clipIngested, this, &MoonLitLibraryWidget::onClipIngested);
	connect(jobs_, &MoonLit::ClipJobs::clipRemoved, this, &MoonLitLibraryWidget::onClipRemoved);
	connect(jobs_, &MoonLit::ClipJobs::clipEditsSaved, this, &MoonLitLibraryWidget::onClipEditsSaved);
	connect(jobs_, &MoonLit::ClipJobs::searchResults, this, &MoonLitLibraryWidget::onSearchResults);
	connect(jobs_, &MoonLit::ClipJobs::previewFrameReady, this, &MoonLitLibraryWidget::onPreviewFrameReady);
	connect(jobs_, &MoonLit::ClipJobs::previewStripReady, this, &MoonLitLibraryWidget::onPreviewStripReady);
	connect(jobs_, &MoonLit::ClipJobs::timelineSaved, this, &MoonLitLibraryWidget::onTimelineSaved);
	connect(jobs_, &MoonLit::ClipJobs::timelinesLoaded, this, &MoonLitLibraryWidget::onTimelinesLoaded);
	connect(jobs_, &MoonLit::ClipJobs::timelineDeleted, this, &MoonLitLibraryWidget::onTimelineDeleted);
	connect(jobs_, &MoonLit::ClipJobs::timelineLoaded, this, &MoonLitLibraryWidget::onTimelineLoaded);

	/* The repository is opened once here and shared by the two workers. */
	{
		QString openError;
		repository_.open(&openError);
	}

	queue_ = new MoonLit::ExportQueue(&repository_);
	connect(queue_, &MoonLit::ExportQueue::exportProgress, this, &MoonLitLibraryWidget::onExportProgress);
	connect(queue_, &MoonLit::ExportQueue::exportFinished, this, &MoonLitLibraryWidget::onExportFinished);
	qRegisterMetaType<QVector<QImage>>("QVector<QImage>");
	qRegisterMetaType<MoonLit::TimelineProject>("MoonLit::TimelineProject");

	connect(timelineEditor_, &MoonLitTimelineEditor::backRequested, this,
		[this]() { stack_->setCurrentWidget(libraryPage_); });
	connect(timelineEditor_, &MoonLitTimelineEditor::statusMessage, this, &MoonLitLibraryWidget::setStatus);
	connect(timelineEditor_, &MoonLitTimelineEditor::saveRequested, this,
		[this](const MoonLit::TimelineProject &project) {
			QMetaObject::invokeMethod(jobs_, [this, project]() { jobs_->saveTimeline(project); },
						  Qt::QueuedConnection);
		});
	connect(timelineEditor_, &MoonLitTimelineEditor::exportRequested, this, [this](const QString &timelineId) {
		cancelButton_->setEnabled(true);
		setStatus(QStringLiteral("Exportando timeline..."));
		if (queue_) {
			queue_->enqueueTimeline(timelineId);
		}
	});

	workerThread_->start();
	queue_->start();

	refresh();
}

MoonLitLibraryWidget::~MoonLitLibraryWidget()
{
	if (workerThread_ && workerThread_->isRunning() && QThread::currentThread() != workerThread_) {
		QMetaObject::invokeMethod(jobs_, []() {}, Qt::BlockingQueuedConnection);
		workerThread_->quit();
		workerThread_->wait();
	}
	/* The queue owns its worker thread; stop it and release the object
	 * before the shared repository member is destroyed. */
	if (queue_) {
		queue_->shutdown();
		delete queue_;
		queue_ = nullptr;
	}
}

void MoonLitLibraryWidget::setStatus(const QString &status, bool error)
{
	statusLabel_->setText(status);
	statusLabel_->setStyleSheet(error ? QStringLiteral("color: %1;").arg(MoonLitTheme::css(MoonLitTheme::rec()))
					  : QString());
}

void MoonLitLibraryWidget::refresh()
{
	if (!jobs_)
		return;

	const QString query = searchEdit_->text().trimmed();
	if (query.isEmpty())
		QMetaObject::invokeMethod(jobs_, [this]() { jobs_->reload(); }, Qt::QueuedConnection);
	else
		QMetaObject::invokeMethod(jobs_, [this, query]() { jobs_->search(query); }, Qt::QueuedConnection);
}

void MoonLitLibraryWidget::ingestClip(const QString &path)
{
	if (path.isEmpty() || !jobs_)
		return;

	setStatus(QStringLiteral("Agregando clip..."));
	QMetaObject::invokeMethod(jobs_, [this, path]() { jobs_->ingest(path); }, Qt::QueuedConnection);
}

std::optional<MoonLit::Clip> MoonLitLibraryWidget::selectedClip() const
{
	if (selectedId_.isEmpty()) {
		return std::nullopt;
	}

	for (const MoonLit::Clip &clip : clips_) {
		if (clip.id == selectedId_) {
			return clip;
		}
	}
	return std::nullopt;
}

void MoonLitLibraryWidget::selectClip(const QString &id)
{
	selectedId_ = id;
	updateSelection();
	if (gridScroll_ && gridCards_.contains(id)) {
		gridScroll_->ensureWidgetVisible(gridCards_.value(id));
	}
}

void MoonLitLibraryWidget::populateList(const QVector<MoonLit::Clip> &clips)
{
	clips_ = clips;
	const LibraryFilter filter = static_cast<LibraryFilter>(filterCombo_->currentIndex());

	while (QLayoutItem *item = gridLayout_->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	gridCards_.clear();
	if (selectedId_.isEmpty() && !clips.isEmpty()) {
		selectedId_ = clips.first().id;
	}

	int shown = 0;
	for (const MoonLit::Clip &clip : clips) {
		const bool missing = clip.missing;
		if (filter == LibraryFilter::Available && missing)
			continue;
		if (filter == LibraryFilter::Missing && !missing)
			continue;

		QString text = clip.title;
		if (clip.metadata.durationMs > 0) {
			text += QStringLiteral("\n%1 s").arg(clip.metadata.durationMs / 1000);
		}
		if (missing) {
			text += QStringLiteral("\n[Faltante]");
		}

		auto *card = new QPushButton(text, gridContainer_);
		card->setProperty("clipId", clip.id);
		card->setFixedSize(190, 120);
		card->setIconSize(QSize(176, 66));
		card->setToolTip(clip.mediaPath);
		if (QFileInfo::exists(clip.thumbnailPath)) {
			card->setIcon(QIcon(clip.thumbnailPath));
		} else {
			card->setIcon(QIcon(QStringLiteral(":/res/images/moonlit-icon.png")));
		}
		card->setStyleSheet(QStringLiteral(
			"QPushButton { background: %1; border: 1px solid %2; border-radius: 8px;"
			" text-align: center; color: %3; padding: 4px; font-size: 11px; }"
			"QPushButton:hover { border-color: %4; }"
			"QPushButton[selected=\"true\"] { border: 2px solid %4; }")
					      .arg(MoonLitTheme::css(MoonLitTheme::bgSurface()),
						   MoonLitTheme::css(MoonLitTheme::border()),
						   MoonLitTheme::css(MoonLitTheme::text()),
						   MoonLitTheme::css(MoonLitTheme::accent())));
		card->setCheckable(true);
		card->setChecked(clip.id == selectedId_);
		card->setProperty("selected", clip.id == selectedId_);

		connect(card, &QPushButton::clicked, this, [this, id = clip.id]() {
			selectedId_ = id;
			updateSelection();
			for (auto it = gridCards_.cbegin(); it != gridCards_.cend(); ++it) {
				const bool selected = it.key() == id;
				it.value()->setChecked(selected);
				it.value()->setProperty("selected", selected);
				it.value()->style()->unpolish(it.value());
				it.value()->style()->polish(it.value());
			}
			/* A second click shortly after the first opens the clip. */
			if (lastCardClick_.isValid() && lastCardClick_.elapsed() < 400) {
				openSelected();
			}
			lastCardClick_.restart();
		});

		gridLayout_->addWidget(card, shown / 3, shown % 3);
		gridCards_.insert(clip.id, card);
		++shown;
	}
	gridLayout_->setRowStretch(shown / 3, 1);

	setStatus(QStringLiteral("%1 clip(s) local(es)").arg(shown));
	emit libraryUpdated(clips);
	updateSelection();
}

void MoonLitLibraryWidget::onFilterChanged(int)
{
	if (!gridScroll_)
		return;
	populateList(clips_);
}

void MoonLitLibraryWidget::importFiles()
{
	const QStringList files = QFileDialog::getOpenFileNames(
		this, QStringLiteral("Importar clips"),
		QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
		QStringLiteral("Video (%1)").arg(QStringLiteral("*.mkv *.mp4 *.mov *.avi *.webm *.ts *.flv *.m4v")));
	if (files.isEmpty() || !jobs_)
		return;

	setStatus(QStringLiteral("Importando %1 archivo(s)...").arg(files.size()));
	QMetaObject::invokeMethod(
		jobs_,
		[this, files]() {
			QDir clipsDir(paths_.clipsPath());
			if (!clipsDir.mkpath(QStringLiteral(".")))
				return;

			for (const QString &source : files) {
				const QString target = uniqueClipDestination(clipsDir, QFileInfo(source).fileName());
				if (!QFile::copy(source, target)) {
					continue;
				}
				jobs_->ingest(target);
			}
		},
		Qt::QueuedConnection);
}

void MoonLitLibraryWidget::onLibraryLoaded(QVector<MoonLit::Clip> clips, const QString &error)
{
	if (!error.isEmpty())
		setStatus(error, true);
	populateList(clips);
}

void MoonLitLibraryWidget::onClipIngested(const QString &id, const QString &error)
{
	if (!error.isEmpty())
		setStatus(error, true);
	refresh();
}

void MoonLitLibraryWidget::onClipRemoved(const QString &id, const QString &error)
{
	if (!error.isEmpty())
		setStatus(error, true);
	refresh();
}

void MoonLitLibraryWidget::onSearchResults(QVector<MoonLit::Clip> clips, const QString &query)
{
	if (!query.trimmed().isEmpty())
		setStatus(QStringLiteral("Resultados para \"%1\"").arg(query.trimmed()));
	populateList(clips);
}

void MoonLitLibraryWidget::onExportProgress(double fraction)
{
	setStatus(QStringLiteral("Exportando... %1 %").arg(qRound(fraction * 100.0)));
}

void MoonLitLibraryWidget::onExportFinished(bool succeeded, bool cancelled, const QString &outputPath,
					    const QString &error)
{
	cancelButton_->setEnabled(false);
	if (cancelled)
		setStatus(QStringLiteral("Exportacion cancelada"));
	else if (succeeded)
		setStatus(QStringLiteral("Exportacion terminada: %1").arg(outputPath));
	else
		setStatus(error, true);
}

void MoonLitLibraryWidget::updateSelection()
{
	const auto clip = selectedClip();
	const bool enabled = clip.has_value();
	openButton_->setEnabled(enabled && !clip->missing);
	revealButton_->setEnabled(enabled && !clip->missing);
	removeButton_->setEnabled(enabled);
	exportButton_->setEnabled(enabled && !clip->missing);
	if (!enabled) {
		detailsLabel_->setText(QStringLiteral("Selecciona un clip"));
		return;
	}

	const QString details = QStringLiteral("%1\n\nArchivo: %2\nVideo: %3\nAudio: %4\nResolucion: %5x%6")
					.arg(clip->title, clip->mediaPath, clip->metadata.videoCodec,
					     clip->metadata.audioCodec)
					.arg(clip->metadata.width)
					.arg(clip->metadata.height);
	detailsLabel_->setText(details);
	const int durationSeconds = clip->metadata.durationMs > 0 ? static_cast<int>(clip->metadata.durationMs / 1000) : 0;
	startSeconds_->setRange(0, std::max(0, durationSeconds));
	endSeconds_->setRange(0, std::max(0, durationSeconds));
	startSeconds_->setValue(static_cast<int>(clip->trimStartMs / 1000));
	endSeconds_->setValue(clip->trimEndMs > 0 ? static_cast<int>(clip->trimEndMs / 1000) : durationSeconds);

	muteCheck_->setChecked(clip->muted);
	gainSlider_->setEnabled(!clip->muted);
	gainSlider_->setValue(qBound(-20, static_cast<int>(std::lround(clip->gainDb)), 20));
	gainValue_->setText(QStringLiteral("%1 dB").arg(gainSlider_->value()));
	saveEditsButton_->setEnabled(true);

	previewPath_ = clip->mediaPath;
	previewDurationMs_ = clip->metadata.durationMs;
	previewImage_->setText(QStringLiteral("Vista previa"));
	frameStrip_->setFrames({}, previewDurationMs_);
	frameStrip_->setTrim(clip->trimStartMs, clip->trimEndMs);
	QMetaObject::invokeMethod(jobs_,
				  [this, path = clip->mediaPath]() { jobs_->previewStrip(path, 12); },
				  Qt::QueuedConnection);
}

void MoonLitLibraryWidget::saveEdits()
{
	const auto clip = selectedClip();
	if (!clip || !jobs_)
		return;

	const int start = startSeconds_->value();
	const int end = endSeconds_->value();
	if (end > 0 && end <= start) {
		setStatus(QStringLiteral("El final debe ser mayor que el inicio"), true);
		return;
	}

	setStatus(QStringLiteral("Guardando edicion..."));
	QMetaObject::invokeMethod(jobs_,
				  [this, id = clip->id, startMs = static_cast<qint64>(start) * 1000,
				   endMs = end > 0 ? static_cast<qint64>(end) * 1000 : -1, muted = muteCheck_->isChecked(),
				   gainDb = static_cast<double>(gainSlider_->value())]() {
					  jobs_->saveEdits(id, startMs, endMs, muted, gainDb);
				  },
				  Qt::QueuedConnection);
}

void MoonLitLibraryWidget::onClipEditsSaved(const QString &, const QString &error)
{
	if (!error.isEmpty()) {
		setStatus(error, true);
		return;
	}
	setStatus(QStringLiteral("Edicion guardada"));
	refresh();
}

void MoonLitLibraryWidget::onPreviewStripReady(const QString &path, const QVector<QImage> &images,
					       const QString &error)
{
	if (path != previewPath_)
		return;
	if (!error.isEmpty()) {
		setStatus(error, true);
		return;
	}
	frameStrip_->setFrames(images, previewDurationMs_);
	if (!images.isEmpty() && !previewImage_->text().isEmpty()) {
		previewImage_->setPixmap(QPixmap::fromImage(
			images.first().scaled(previewImage_->width() - 4, previewImage_->height() - 4,
					      Qt::KeepAspectRatio, Qt::SmoothTransformation)));
		previewImage_->setText(QString());
	}
}

void MoonLitLibraryWidget::onPreviewFrameReady(const QString &path, qint64 positionMs, const QImage &image,
					       const QString &error)
{
	Q_UNUSED(positionMs);
	if (path != previewPath_)
		return;
	if (!error.isEmpty() || image.isNull()) {
		if (!error.isEmpty()) {
			setStatus(error, true);
		}
		return;
	}
	previewImage_->setPixmap(QPixmap::fromImage(
		image.scaled(previewImage_->width() - 4, previewImage_->height() - 4, Qt::KeepAspectRatio,
			     Qt::SmoothTransformation)));
	previewImage_->setText(QString());
}

void MoonLitLibraryWidget::openSelected()
{
	const auto clip = selectedClip();
	if (clip && !clip->missing)
		QDesktopServices::openUrl(QUrl::fromLocalFile(clip->mediaPath));
}

void MoonLitLibraryWidget::revealSelected()
{
	const auto clip = selectedClip();
	if (!clip || clip->missing)
		return;

	if (platform_) {
		platform_->revealInFileManager(clip->mediaPath.toStdWString());
	}
}

void MoonLitLibraryWidget::removeSelected()
{
	const auto clip = selectedClip();
	if (!clip)
		return;

	if (QMessageBox::question(this, QStringLiteral("Eliminar clip"),
				 QStringLiteral("Enviar este clip a la papelera?")) != QMessageBox::Yes)
		return;

	if (!clip->missing && !QFile::moveToTrash(clip->mediaPath)) {
		setStatus(QStringLiteral("No se pudo mover el clip a la papelera"), true);
		return;
	}

	QMetaObject::invokeMethod(jobs_, [this, id = clip->id]() { jobs_->removeClip(id); }, Qt::QueuedConnection);
}

void MoonLitLibraryWidget::exportSelected()
{
	const auto clip = selectedClip();
	if (!clip || clip->missing)
		return;

	const int start = startSeconds_->value();
	const int end = endSeconds_->value();
	if (end > 0 && end <= start) {
		setStatus(QStringLiteral("El final debe ser mayor que el inicio"), true);
		return;
	}

	cancelButton_->setEnabled(true);
	setStatus(QStringLiteral("Exportando..."));
	if (queue_) {
		queue_->enqueueTrim(clip->id, static_cast<qint64>(start) * 1000,
				    end > 0 ? static_cast<qint64>(end) * 1000 : -1);
	}
}

void MoonLitLibraryWidget::openTimelineEditor()
{
	if (!stack_ || !timelineEditor_) {
		return;
	}
	timelineEditor_->setClips(clips_);
	stack_->setCurrentWidget(timelineEditor_);
	QMetaObject::invokeMethod(jobs_, [this]() { jobs_->listTimelines(); }, Qt::QueuedConnection);
}

void MoonLitLibraryWidget::onTimelineSaved(const QString &, const QString &error)
{
	if (!error.isEmpty()) {
		setStatus(error, true);
		return;
	}
	setStatus(QStringLiteral("Timeline guardado"));
}

void MoonLitLibraryWidget::onTimelinesLoaded(const QVector<MoonLit::TimelineProject> &projects,
					     const QString &error)
{
	if (!error.isEmpty()) {
		setStatus(error, true);
		return;
	}
	if (projects.isEmpty()) {
		timelineEditor_->setProject(MoonLit::TimelineProject::create(QStringLiteral("Nuevo timeline")));
		return;
	}
	QMetaObject::invokeMethod(jobs_, [this, id = projects.first().id]() { jobs_->loadTimeline(id); },
				  Qt::QueuedConnection);
}

void MoonLitLibraryWidget::onTimelineDeleted(const QString &, const QString &error)
{
	if (!error.isEmpty()) {
		setStatus(error, true);
		return;
	}
	setStatus(QStringLiteral("Timeline eliminado"));
}

void MoonLitLibraryWidget::onTimelineLoaded(const MoonLit::TimelineProject &project, const QString &error)
{
	if (!error.isEmpty()) {
		setStatus(error, true);
		return;
	}
	timelineEditor_->setProject(project);
}
