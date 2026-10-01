/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "daydialog.h"
#include "ui_daydialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>

DayDialog::DayDialog(QWidget *parent) : QDialog(parent), ui(new Ui::DayDialog) {
	ui->setupUi(this);

	ui->dayTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	ui->dayTable->setSelectionMode(QAbstractItemView::SingleSelection);
	ui->dayTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	ui->dayTable->verticalHeader()->setVisible(false);
	ui->dayTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	ui->dayTable->horizontalHeader()->setStretchLastSection(true);

	connect(ui->dayTable, &QTableWidget::itemSelectionChanged, this, &DayDialog::updateOk);
	connect(ui->dayTable, &QTableWidget::cellDoubleClicked, this, &QDialog::accept);
	connect(ui->tabWidget, &QTabWidget::currentChanged, this, &DayDialog::updateOk);
	connect(ui->deleteButton, &QPushButton::clicked, this, &DayDialog::deleteSelected);
	connect(ui->dayTitle, &QLineEdit::textChanged, this, &DayDialog::syncFolderFromTitle);
	connect(ui->dayFolder, &QLineEdit::textChanged, this, &DayDialog::updatePathPreview);
	connect(ui->dayFolder, &QLineEdit::textEdited, this, [this]() { folderEdited = true; });

	updateOk();
}

DayDialog::~DayDialog() { delete ui; }

void DayDialog::setContext(const QString &contestDir, const QList<DayEntry> &days, int currentIndex) {
	this->contestDir = contestDir;

	ui->dayTable->setRowCount(0);
	ui->dayTable->setColumnCount(2);
	ui->dayTable->setHorizontalHeaderLabels({tr("Day"), tr("Location")});

	for (const DayEntry &entry : days) {
		const int row = ui->dayTable->rowCount();
		ui->dayTable->insertRow(row);
		auto *titleItem = new QTableWidgetItem(entry.title);
		titleItem->setTextAlignment(Qt::AlignCenter);
		ui->dayTable->setItem(row, 0, titleItem);
		ui->dayTable->setItem(row, 1, new QTableWidgetItem(QFileInfo(entry.file).path()));
	}

	if (days.isEmpty()) {
		ui->tabWidget->setCurrentIndex(1); // 没有比赛日时直接到「新建」
	} else if (currentIndex >= 0 && currentIndex < days.size()) {
		ui->dayTable->selectRow(currentIndex);
	}

	ui->dayTitle->setText(tr("Day %1").arg(days.size() + 1));
	folderEdited = false;
	ui->dayFolder->setText(ui->dayTitle->text());
	updatePathPreview();
	updateOk();
}

void DayDialog::preferNewTab() { ui->tabWidget->setCurrentIndex(1); }

bool DayDialog::isNewDay() const { return ui->tabWidget->currentIndex() == 1; }

bool DayDialog::isDeleteRequested() const { return deleteRequested; }

int DayDialog::getSelectedIndex() const { return ui->dayTable->currentRow(); }

QString DayDialog::getDayTitle() const { return ui->dayTitle->text().trimmed(); }

QString DayDialog::getDayFileName() const { return ui->dayFolder->text().trimmed(); }

void DayDialog::syncFolderFromTitle() {
	if (! folderEdited)
		ui->dayFolder->setText(ui->dayTitle->text().trimmed());

	updateOk();
}

void DayDialog::updatePathPreview() {
	const QString folder = ui->dayFolder->text().trimmed();
	ui->dayPath->setText(folder.isEmpty() ? QString() : QDir(contestDir).absoluteFilePath(folder));
	updateOk();
}

void DayDialog::updateOk() {
	const bool ok = (ui->tabWidget->currentIndex() == 0)
	                    ? (ui->dayTable->currentRow() >= 0)
	                    : (! ui->dayTitle->text().trimmed().isEmpty() &&
	                       ! ui->dayFolder->text().trimmed().isEmpty());
	ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(ok);
	ui->deleteButton->setEnabled(ui->dayTable->currentRow() >= 0);
}

void DayDialog::deleteSelected() {
	const int row = ui->dayTable->currentRow();

	if (row < 0)
		return;

	const QTableWidgetItem *item = ui->dayTable->item(row, 0);
	const QString title = item ? item->text() : QString();

	if (QMessageBox::question(this, tr("Delete Contest Day"),
	                          tr("Delete contest day \"%1\" and all of its content?\n\nThis cannot be "
	                             "undone.")
	                              .arg(title),
	                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;

	deleteRequested = true;
	accept();
}
