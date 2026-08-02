#pragma once

#include <QWidget>

#include <moonlit/Clip.hpp>
#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/services/ClipJobs.hpp>

#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QThread;
class QTimer;

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
	void onLibraryLoaded(QVector<MoonLit::Clip> clips, const QString &error);
	void onClipIngested(const QString &id, const QString &error);
	void onClipRemoved(const QString &id, const QString &error);
	void onSearchResults(QVector<MoonLit::Clip> clips, const QString &query);
	void onExportProgress(double fraction);
	void onExportFinished(bool succeeded, bool cancelled, const QString &outputPath, const QString &error);

private:
	void setStatus(const QString &status, bool error = false);
	std::optional<MoonLit::Clip> selectedClip() const;
	void populateList(const QVector<MoonLit::Clip> &clips);
	void scheduleSearch();

	MoonLit::MoonLitPaths paths_ = MoonLit::MoonLitPaths::defaultPaths();
	MoonLit::ClipJobs *jobs_ = nullptr;
	QThread *workerThread_ = nullptr;
	QTimer *searchDebounceTimer_ = nullptr;
	QVector<MoonLit::Clip> clips_;

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
	QPushButton *cancelButton_ = nullptr;
};
