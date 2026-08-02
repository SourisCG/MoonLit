#pragma once

#include <QWidget>

#include <moonlit/Clip.hpp>
#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/services/ClipJobs.hpp>

#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QThread;
class QTimer;
class ClipFrameStrip;

class MoonLitLibraryWidget final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitLibraryWidget(QWidget *parent = nullptr);
	~MoonLitLibraryWidget() override;

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
	void importFiles();
	void onFilterChanged(int index);
	void saveEdits();
	void onLibraryLoaded(QVector<MoonLit::Clip> clips, const QString &error);
	void onClipIngested(const QString &id, const QString &error);
	void onClipRemoved(const QString &id, const QString &error);
	void onClipEditsSaved(const QString &id, const QString &error);
	void onSearchResults(QVector<MoonLit::Clip> clips, const QString &query);
	void onExportProgress(double fraction);
	void onExportFinished(bool succeeded, bool cancelled, const QString &outputPath, const QString &error);
	void onPreviewStripReady(const QString &path, const QVector<QImage> &images, const QString &error);
	void onPreviewFrameReady(const QString &path, qint64 positionMs, const QImage &image, const QString &error);

private:
	enum class LibraryFilter { All, Available, Missing };

	void setStatus(const QString &status, bool error = false);
	std::optional<MoonLit::Clip> selectedClip() const;
	void populateList(const QVector<MoonLit::Clip> &clips);

	MoonLit::MoonLitPaths paths_ = MoonLit::MoonLitPaths::defaultPaths();
	MoonLit::ClipJobs *jobs_ = nullptr;
	QThread *workerThread_ = nullptr;
	QTimer *searchDebounceTimer_ = nullptr;
	QVector<MoonLit::Clip> clips_;

	QLineEdit *searchEdit_ = nullptr;
	QComboBox *filterCombo_ = nullptr;
	QListWidget *clipList_ = nullptr;
	QLabel *detailsLabel_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QLabel *previewImage_ = nullptr;
	QLabel *gainValue_ = nullptr;
	ClipFrameStrip *frameStrip_ = nullptr;
	QCheckBox *muteCheck_ = nullptr;
	QSlider *gainSlider_ = nullptr;
	QSpinBox *startSeconds_ = nullptr;
	QSpinBox *endSeconds_ = nullptr;
	QPushButton *openButton_ = nullptr;
	QPushButton *revealButton_ = nullptr;
	QPushButton *removeButton_ = nullptr;
	QPushButton *exportButton_ = nullptr;
	QPushButton *cancelButton_ = nullptr;
	QPushButton *saveEditsButton_ = nullptr;
	QString previewPath_;
	qint64 previewDurationMs_ = 0;
};
