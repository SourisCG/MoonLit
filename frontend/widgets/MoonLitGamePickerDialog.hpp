#pragma once

#include <moonlit/capture/WindowsTarget.hpp>

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

/* Medal-style manual game picker: lists running top-level windows so the
 * user can pin a game the automatic detector does not recognize. Optionally
 * remembers the executable so it is detected automatically next time. */
class MoonLitGamePickerDialog final : public QDialog {
	Q_OBJECT

public:
	explicit MoonLitGamePickerDialog(QWidget *parent = nullptr);

	MoonLitTarget selectedTarget() const;
	bool rememberRequested() const;

private slots:
	void refreshList();
	void filterChanged(const QString &text);

private:
	void populate(const QVector<MoonLitTarget> &targets);

	QLineEdit *search = nullptr;
	QTableWidget *list = nullptr;
	QPushButton *refreshButton = nullptr;
	QPushButton *captureButton = nullptr;
	QCheckBox *rememberCheck = nullptr;
	QVector<MoonLitTarget> targets_;
	QString filter_;
};
