#pragma once

#include <QWidget>

#include <memory>

#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/editor/ClipExportService.hpp>
#include <moonlit/media/MediaProbe.hpp>
#include <moonlit/media/ThumbnailService.hpp>
#include <moonlit/persistence/FileClipRepository.hpp>

#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

class MoonLitLibraryWidget final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitLibraryWidget(QWidget *parent = nullptr);

public slots:
	void refresh();
	void ingestClip(const QString &path);

signals:
	void backRequested();

private slots:
	void updateSelection();
	void openSelected();
	void revealSelected();
	void removeSelected();
	void exportSelected();

private:
	void setStatus(const QString &status, bool error = false);
	std::optional<MoonLit::Clip> selectedClip() const;

	MoonLit::MoonLitPaths paths_ = MoonLit::MoonLitPaths::defaultPaths();
	MoonLit::FileClipRepository repository_{paths_};
	MoonLit::FfmpegMediaProbe probe_;
	MoonLit::FfmpegThumbnailService thumbnails_;
	MoonLit::FfmpegClipExportService exporter_;

	QLineEdit *searchEdit_ = nullptr;
	QListWidget *clipList_ = nullptr;
	QLabel *detailsLabel_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QSpinBox *startSeconds_ = nullptr;
	QSpinBox *endSeconds_ = nullptr;
	QPushButton *openButton_ = nullptr;
	QPushButton *revealButton_ = nullptr;
	QPushButton *removeButton_ = nullptr;
	QPushButton *exportButton_ = nullptr;
};
