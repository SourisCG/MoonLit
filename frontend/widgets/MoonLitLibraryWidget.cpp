#include "MoonLitLibraryWidget.hpp"

#include <QDesktopServices>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString clipSummary(const MoonLit::Clip &clip)
{
	const QString duration = clip.metadata.durationMs > 0
				? QStringLiteral("%1 s").arg(clip.metadata.durationMs / 1000)
				: QStringLiteral("duracion desconocida");
	return QStringLiteral("%1\n%2 | %3").arg(clip.title, duration, QFileInfo(clip.mediaPath).fileName());
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

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(28, 24, 28, 24);
	root->setSpacing(12);

	auto *header = new QHBoxLayout();
	auto *backButton = new QPushButton(QStringLiteral("Volver"), this);
	auto *title = new QLabel(QStringLiteral("Biblioteca"), this);
	title->setObjectName(QStringLiteral("libraryTitle"));
	auto *refreshButton = new QPushButton(QStringLiteral("Actualizar"), this);
	header->addWidget(backButton);
	header->addWidget(title);
	header->addStretch(1);
	header->addWidget(refreshButton);
	root->addLayout(header);

	searchEdit_ = new QLineEdit(this);
	searchEdit_->setPlaceholderText(QStringLiteral("Buscar clips, juegos o archivos..."));
	root->addWidget(searchEdit_);

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
	exportButton_ = new QPushButton(QStringLiteral("Exportar MP4"), this);
	exportButton_->setEnabled(false);
	details->addWidget(exportButton_);
	details->addStretch(1);
	content->addLayout(details, 1);
	root->addLayout(content, 1);

	statusLabel_ = new QLabel(this);
	statusLabel_->setObjectName(QStringLiteral("libraryStatus"));
	statusLabel_->setWordWrap(true);
	root->addWidget(statusLabel_);

	connect(backButton, &QPushButton::clicked, this, &MoonLitLibraryWidget::backRequested);
	connect(refreshButton, &QPushButton::clicked, this, &MoonLitLibraryWidget::refresh);
	connect(searchEdit_, &QLineEdit::textChanged, this, &MoonLitLibraryWidget::refresh);
	connect(clipList_, &QListWidget::currentRowChanged, this, &MoonLitLibraryWidget::updateSelection);
	connect(clipList_, &QListWidget::itemDoubleClicked, this, [this]() { openSelected(); });
	connect(openButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::openSelected);
	connect(revealButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::revealSelected);
	connect(removeButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::removeSelected);
	connect(exportButton_, &QPushButton::clicked, this, &MoonLitLibraryWidget::exportSelected);

	refresh();
}

void MoonLitLibraryWidget::setStatus(const QString &status, bool error)
{
	statusLabel_->setText(status);
	statusLabel_->setStyleSheet(error ? QStringLiteral("color: #e98b8b;") : QString());
}

void MoonLitLibraryWidget::refresh()
{
	QString error;
	if (!repository_.open(&error) && !repository_.reload(&error)) {
		setStatus(error, true);
		return;
	}

	repository_.reconcile(nullptr, &error);
	const QString query = searchEdit_ ? searchEdit_->text().trimmed() : QString();
	const auto clips = repository_.list(true);
	clipList_->clear();
	for (const MoonLit::Clip &clip : clips) {
		if (!query.isEmpty() && !clip.title.contains(query, Qt::CaseInsensitive) &&
		    !clip.mediaPath.contains(query, Qt::CaseInsensitive)) {
			continue;
		}

		auto *item = new QListWidgetItem(clipSummary(clip), clipList_);
		item->setData(Qt::UserRole, clip.id);
		if (QFileInfo::exists(clip.thumbnailPath))
			item->setIcon(QIcon(clip.thumbnailPath));
		if (clip.missing)
			item->setForeground(QColor(QStringLiteral("#e98b8b")));
	}

	setStatus(QStringLiteral("%1 clip(s) local(es)").arg(clipList_->count()));
	updateSelection();
}

void MoonLitLibraryWidget::ingestClip(const QString &path)
{
	if (path.isEmpty())
		return;

	QString error;
	if (!repository_.open(&error)) {
		setStatus(error, true);
		return;
	}
	if (repository_.findByMediaPath(path)) {
		refresh();
		return;
	}

	MoonLit::Clip clip = MoonLit::Clip::create(path);
	if (const auto metadata = probe_.probe(path, &error))
		clip.metadata = *metadata;
	else
		setStatus(error, true);

	clip.thumbnailPath = paths_.thumbnailPath(clip.id);
	const auto stored = repository_.upsert(clip, &error);
	if (!stored) {
		setStatus(error, true);
		return;
	}

	QString thumbnailError;
	const qint64 timestamp = stored->metadata.durationMs > 0 ? stored->metadata.durationMs / 4 : 0;
	MoonLit::ThumbnailOptions options;
	options.timestampMs = timestamp;
	if (!thumbnails_.writeThumbnail(stored->mediaPath, stored->thumbnailPath, options, &thumbnailError))
		setStatus(QStringLiteral("Clip guardado; thumbnail pendiente: %1").arg(thumbnailError), true);
	else
		setStatus(QStringLiteral("Clip guardado en la biblioteca"));

	refresh();
}

std::optional<MoonLit::Clip> MoonLitLibraryWidget::selectedClip() const
{
	const QListWidgetItem *item = clipList_->currentItem();
	if (!item)
		return std::nullopt;
	return repository_.find(item->data(Qt::UserRole).toString());
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

	QString error;
	if (!repository_.remove(clip->id, &error)) {
		setStatus(error, true);
		return;
	}
	refresh();
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

	MoonLit::ClipExportRequest request;
	request.sourcePath = clip->mediaPath;
	request.destinationPath = paths_.exportPath(clip->id, QStringLiteral("mp4"));
	request.startMs = static_cast<qint64>(start) * 1000;
	request.endMs = end > 0 ? static_cast<qint64>(end) * 1000 : -1;
	const MoonLit::ClipExportResult result = exporter_.exportClip(request);
	if (!result.succeeded) {
		setStatus(result.error, true);
		return;
	}

	setStatus(QStringLiteral("Exportacion terminada: %1").arg(result.outputPath));
}
