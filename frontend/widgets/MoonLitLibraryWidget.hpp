#pragma once

#include <QWidget>

#include <moonlit/Clip.hpp>
#include <moonlit/editor/Timeline.hpp>
#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/persistence/SqliteClipRepository.hpp>
#include <moonlit/platform/IPlatformServices.hpp>
#include <moonlit/services/ClipJobs.hpp>
#include <moonlit/services/ExportQueue.hpp>

#include <QHash>
#include <QElapsedTimer>

#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QThread;
class QTimer;
class ClipFrameStrip;
class MoonLitThumbCard;
class MoonLitTimelineEditor;
class QStackedWidget;

class MoonLitLibraryWidget final : public QWidget {
	Q_OBJECT

public:
	explicit MoonLitLibraryWidget(QWidget *parent = nullptr);
	~MoonLitLibraryWidget() override;

public slots:
	void refresh();
	void ingestClip(const QString &path);
	void selectClip(const QString &id);

signals:
	void backRequested();
	void libraryUpdated(QVector<MoonLit::Clip> clips);

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
	void onTimelineSaved(const QString &id, const QString &error);
	void onTimelinesLoaded(const QVector<MoonLit::TimelineProject> &projects, const QString &error);
	void onTimelineDeleted(const QString &id, const QString &error);
	void onTimelineLoaded(const MoonLit::TimelineProject &project, const QString &error);
	void onRecentLoaded(QVector<MoonLit::Clip> clips, const QString &error);

private:
	enum class LibraryFilter { All, Available, Missing };

	void setStatus(const QString &status, bool error = false);
	std::optional<MoonLit::Clip> selectedClip() const;
	void populateList(const QVector<MoonLit::Clip> &clips);
	void reflowGrid();
	void loadRecentClips();
	void openTimelineEditor();

protected:
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;

private:

	MoonLit::MoonLitPaths paths_ = MoonLit::MoonLitPaths::defaultPaths();
	MoonLit::SqliteClipRepository repository_{paths_};
	std::unique_ptr<MoonLit::IPlatformServices> platform_ = MoonLit::IPlatformServices::create();
	MoonLit::ClipJobs *jobs_ = nullptr;
	MoonLit::ExportQueue *queue_ = nullptr;
	QThread *workerThread_ = nullptr;
	QTimer *searchDebounceTimer_ = nullptr;
	QVector<MoonLit::Clip> clips_;
	QStackedWidget *stack_ = nullptr;
	QWidget *libraryPage_ = nullptr;
	MoonLitTimelineEditor *timelineEditor_ = nullptr;

	QLineEdit *searchEdit_ = nullptr;
	QComboBox *filterCombo_ = nullptr;
	QScrollArea *gridScroll_ = nullptr;
	QWidget *gridContainer_ = nullptr;
	QGridLayout *gridLayout_ = nullptr;
	QHash<QString, MoonLitThumbCard *> gridCards_;
	QString selectedId_;
	QElapsedTimer lastCardClick_;
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
