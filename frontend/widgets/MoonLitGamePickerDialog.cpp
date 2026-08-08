#include "MoonLitGamePickerDialog.hpp"

#include "MoonLitTheme.hpp"

#include <moonlit/capture/WindowsProcessUtil.hpp>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

MoonLitGamePickerDialog::MoonLitGamePickerDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QStringLiteral("Seleccionar juego"));
	setModal(true);
	setMinimumSize(560, 420);

	using namespace MoonLitTheme;
	setStyleSheet(QStringLiteral(
		"QLabel { color: %1; }"
		"QLineEdit { background: %2; border: 1px solid %3; border-radius: 6px;"
		" color: %1; padding: 4px 8px; }"
		"QTableWidget { background: %2; alternate-background-color: %4; color: %1;"
		" border: 1px solid %3; border-radius: 6px; gridline-color: %3; }"
		"QHeaderView::section { background: %5; color: %1; border: 0; padding: 4px 8px; }"
		"QPushButton { min-height: 26px; padding: 0 12px; border: 1px solid %3;"
		" border-radius: 6px; background: %2; color: %1; }"
		"QPushButton:hover { border-color: %6; }"
		"QPushButton:disabled { color: %7; }"
		"QCheckBox { color: %1; }")
				      .arg(css(text()), css(bgSurface()), css(border()), css(bgElevated()),
					   css(bgDeep()), css(accentHover()), css(textMuted())));

	auto *layout = new QVBoxLayout(this);
	layout->setSpacing(10);

	auto *hint = new QLabel(
		QStringLiteral("Elige la ventana del juego a capturar si no fue detectado automáticamente."), this);
	hint->setWordWrap(true);
	layout->addWidget(hint);

	search = new QLineEdit(this);
	search->setPlaceholderText(QStringLiteral("Buscar por proceso o ventana…"));
	layout->addWidget(search);

	list = new QTableWidget(0, 2, this);
	list->setHorizontalHeaderLabels({QStringLiteral("Proceso"), QStringLiteral("Ventana")});
	list->horizontalHeader()->setStretchLastSection(true);
	list->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	list->setSelectionBehavior(QAbstractItemView::SelectRows);
	list->setSelectionMode(QAbstractItemView::SingleSelection);
	list->setEditTriggers(QAbstractItemView::NoEditTriggers);
	list->setAlternatingRowColors(true);
	layout->addWidget(list, 1);

	rememberCheck = new QCheckBox(
		QStringLiteral("Recordar este juego (se detectará automáticamente en el futuro)"), this);
	layout->addWidget(rememberCheck);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	captureButton = new QPushButton(QStringLiteral("Capturar"), buttons);
	captureButton->setEnabled(false);
	buttons->addButton(captureButton, QDialogButtonBox::AcceptRole);
	refreshButton = new QPushButton(QStringLiteral("Actualizar"), buttons);
	buttons->addButton(refreshButton, QDialogButtonBox::ActionRole);
	layout->addWidget(buttons);

	connect(captureButton, &QPushButton::clicked, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(refreshButton, &QPushButton::clicked, this, &MoonLitGamePickerDialog::refreshList);
	connect(search, &QLineEdit::textChanged, this, &MoonLitGamePickerDialog::filterChanged);
	connect(list, &QTableWidget::itemSelectionChanged, this, [this]() {
		captureButton->setEnabled(list->currentRow() >= 0);
	});
	connect(list, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *item) {
		if (item) {
			accept();
		}
	});

	refreshList();
}

void MoonLitGamePickerDialog::refreshList()
{
	targets_ = MoonLit::WindowsProcessUtil::enumerateTopLevelTargets();
	std::sort(targets_.begin(), targets_.end(),
		  [](const MoonLitTarget &left, const MoonLitTarget &right) {
			  return QString::compare(left.executable, right.executable, Qt::CaseInsensitive) < 0;
		  });
	populate(targets_);
}

void MoonLitGamePickerDialog::filterChanged(const QString &text)
{
	filter_ = text.trimmed().toLower();
	if (filter_.isEmpty()) {
		populate(targets_);
		return;
	}

	QVector<MoonLitTarget> filtered;
	for (const MoonLitTarget &target : targets_) {
		if (target.executable.toLower().contains(filter_) || target.title.toLower().contains(filter_)) {
			filtered.append(target);
		}
	}
	populate(filtered);
}

void MoonLitGamePickerDialog::populate(const QVector<MoonLitTarget> &targets)
{
	list->setRowCount(0);
	for (const MoonLitTarget &target : targets) {
		const int row = list->rowCount();
		list->insertRow(row);
		auto *exe = new QTableWidgetItem(target.executable);
		exe->setData(Qt::UserRole, static_cast<qlonglong>(target.window));
		auto *title = new QTableWidgetItem(target.title);
		list->setItem(row, 0, exe);
		list->setItem(row, 1, title);
	}
	captureButton->setEnabled(list->currentRow() >= 0);
}

MoonLitTarget MoonLitGamePickerDialog::selectedTarget() const
{
	const int row = list->currentRow();
	if (row < 0) {
		return {};
	}
	const qlonglong window = list->item(row, 0)->data(Qt::UserRole).toLongLong();
	for (const MoonLitTarget &target : targets_) {
		if (static_cast<qlonglong>(target.window) == window) {
			return target;
		}
	}
	return {};
}

bool MoonLitGamePickerDialog::rememberRequested() const
{
	return rememberCheck->isChecked();
}
