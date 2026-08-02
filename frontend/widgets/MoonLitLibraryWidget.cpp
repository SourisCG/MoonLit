#include "MoonLitLibraryWidget.hpp"

#include "ClipFrameStrip.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QProcess>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QString clipSummary(const MoonLit::Clip &clip)
{
	const QString duration = clip.metadata.durationMs > 0
				? QStringLiteral("%1 s").arg(clip.metadata.durationMs / 1000)
				: QStringLiteral("duracion desconocida");
	return QStringLiteral("%1\n%2 | %3").arg(clip.title, duration, QFileInfo(clip.mediaPath).fileName());
}

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
	setObjectName(QStringLiteral("moonlitLibrary"));
	setStyleSheet(QStringLiteral(R"(
        #moonlitLibrary { background: #111318; color: #f2f4f8; }
        QLabel#libraryTitle { color: #ffffff; font-size: 24px; font-weight: 700; }
        QLabel#libraryDetails, QLabel#libraryStatus { color: #9ba3b4; }
        QLineEdit, QListWidget, QSpinBox { background: #1b1e25; color: #f2f4f8; border: 1px solid #343b49; border-radius: 7px; padding: 7px; }
        QListWidget::item { padding: 8px; border-bottom: 1px solid #2b303b; }
        QListWidget::item:selected { background: #303746; }
        QPushButton { min-height: 34px; padding: 0 12px; border: 1px solid #343b49; border-radius: 7px; background: #252a34; color: #f2f4f8; }
        QPushButton:hover { background: #303746; }
        QPushButton:disabled { color: #697180; background: #1d2027; }
    )"));

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
	header->addWidget(backButton);
	header->addWidget(title);
	header->addStretch(1);
	header->addWidget(importButton);
	header->addWidget(refreshButton);
	root->addLayout(header);

	searchEdit_ = new QLineEdit(this);
	searchEdit_->setPlaceholderText(QStringLiteral("Buscar clips, juegos o archivos..."));
	filterCombo_ = new QComboBox(this);
	filterCombo_->addItems({QStringLiteral("Todos"), QStringLiteral("Disponibles"),
				QStringLiteral("Faltantes")});
	filterCombo_->setCurrentIndex(0);
	auto *searchRow = new QHBoxLayout();
	searchRow->addWidget(searchEdit_, 1);
	searchRow->addWidget(filterCombo_);
	root->addLayout(searchRow);

	auto *content = new QHBoxLayout();
	clipList_ = new QListWidget(this);
	clipList_->setMinimumWidth(420);
	content->addWidget(clipList_, 2);

	auto *details = new QVBoxLayout();
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
	previewImage_->setFixedHeight(110);
	previewImage_->setAlignment(Qt::AlignCenter);
	previewImage_->setStyleSheet(QStringLiteral("background: #000000; border: 1px solid #343b49;"));
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
	content->addLayout(details, 1);
	root->addLayout(content, 1);

	statusLabel_ = new QLabel(this);
	statusLabel_->setObjectName(QStringLiteral("libraryStatus"));
	statusLabel_->setWordWrap(true);
	root->addWidget(statusLabel_);

	connect(backButton, &QPushButton::clicked, this, &MoonLitLibraryWidget::backRequested);
	connect(refreshButton, &QPushButton::clicked, this, &MoonLitLibraryWidget::refresh);
	connect(importButton, &QPushButton::clicked, this, &MoonLitLibraryWidget::importFiles);
	connect(filterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&MoonLitLibraryWidget::onFilterChanged);
	connect(searchEdit_, &QLineEdit::textChanged, this, [this]() { searchDebounceTimer_->start(); });
	connect(clipList_, &QListWidget::currentRowChanged, this, &MoonLitLibraryWidget::updateSelection);
	connect(clipList_, &QListWidget::itemDoubleClicked, this, [this]() { openSelected(); });
	connect(openButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::openSelected);
	connect(revealButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::revealSelected);
	connect(removeButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::removeSelected);
	connect(exportButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::exportSelected);
	connect(cancelButton_, &QPushButton::clicked, this,
		[this]() { setStatus(QStringLiteral("Cancelando...")); jobs_->cancelExport(); });
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
	jobs_ = new MoonLit::ClipJobs(paths_);
	jobs_->moveToThread(workerThread_);
	connect(workerThread_, &QThread::finished, jobs_, &QObject::deleteLater);
	connect(jobs_, &MoonLit::ClipJobs::libraryLoaded, this, &MoonLitLibraryWidget::onLibraryLoaded);
	connect(jobs_, &MoonLit::ClipJobs::clipIngested, this, &MoonLitLibraryWidget::onClipIngested);
	connect(jobs_, &MoonLit::ClipJobs::clipRemoved, this, &MoonLitLibraryWidget::onClipRemoved);
	connect(jobs_, &MoonLit::ClipJobs::clipEditsSaved, this, &MoonLitLibraryWidget::onClipEditsSaved);
	connect(jobs_, &MoonLit::ClipJobs::searchResults, this, &MoonLitLibraryWidget::onSearchResults);
	connect(jobs_, &MoonLit::ClipJobs::exportProgress, this, &MoonLitLibraryWidget::onExportProgress);
	connect(jobs_, &MoonLit::ClipJobs::exportFinished, this, &MoonLitLibraryWidget::onExportFinished);
	connect(jobs_, &MoonLit::ClipJobs::previewFrameReady, this, &MoonLitLibraryWidget::onPreviewFrameReady);
	connect(jobs_, &MoonLit::ClipJobs::previewStripReady, this, &MoonLitLibraryWidget::onPreviewStripReady);
	qRegisterMetaType<QVector<QImage>>("QVector<QImage>");
	workerThread_->start();

	refresh();
}

MoonLitLibraryWidget::~MoonLitLibraryWidget()
{
	if (workerThread_ && workerThread_->isRunning()) {
		QMetaObject::invokeMethod(jobs_, []() {}, Qt::BlockingQueuedConnection);
		workerThread_->quit();
		workerThread_->wait();
	}
}

void MoonLitLibraryWidget::setStatus(const QString &status, bool error)
{
	statusLabel_->setText(status);
	statusLabel_->setStyleSheet(error ? QStringLiteral("color: #e98b8b;") : QString());
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
	const QListWidgetItem *item = clipList_->currentItem();
	if (!item)
		return std::nullopt;

	const QString id = item->data(Qt::UserRole).toString();
	for (const MoonLit::Clip &clip : clips_) {
		if (clip.id == id)
			return clip;
	}
	return std::nullopt;
}

void MoonLitLibraryWidget::populateList(const QVector<MoonLit::Clip> &clips)
{
	clips_ = clips;
	const LibraryFilter filter = static_cast<LibraryFilter>(filterCombo_->currentIndex());
	clipList_->clear();
	int shown = 0;
	for (const MoonLit::Clip &clip : clips) {
		const bool missing = clip.missing;
		if (filter == LibraryFilter::Available && missing)
			continue;
		if (filter == LibraryFilter::Missing && !missing)
			continue;

		auto *item = new QListWidgetItem(clipSummary(clip), clipList_);
		item->setData(Qt::UserRole, clip.id);
		if (QFileInfo::exists(clip.thumbnailPath))
			item->setIcon(QIcon(clip.thumbnailPath));
		if (missing)
			item->setForeground(QColor(QStringLiteral("#e98b8b")));
		++shown;
	}

	setStatus(QStringLiteral("%1 clip(s) local(es)").arg(shown));
	updateSelection();
}

void MoonLitLibraryWidget::onFilterChanged(int)
{
	if (!clipList_)
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

#ifdef Q_OS_WIN
	QProcess::startDetached(QStringLiteral("explorer.exe"),
				{QStringLiteral("/select,"), QDir::toNativeSeparators(clip->mediaPath)});
#else
	QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(clip->mediaPath).absolutePath()));
#endif
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
	QMetaObject::invokeMethod(jobs_,
				  [this, id = clip->id, startMs = static_cast<qint64>(start) * 1000,
				   endMs = end > 0 ? static_cast<qint64>(end) * 1000 : -1]() {
					  jobs_->exportClip(id, startMs, endMs);
				  },
				  Qt::QueuedConnection);
}
