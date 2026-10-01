/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "newtaskdialog.h"

#include "base/settings.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
	/// 标题 → 可作文件名的形式：去掉路径非法字符与空白，其余保留。
	QString toFileName(const QString &title) {
		QString name = title.trimmed();

		for (int i = 0; i < name.size(); i++) {
			const QChar ch = name.at(i);

			if (ch.isSpace() || QStringLiteral("\\/:*?\"<>|").contains(ch))
				name[i] = QLatin1Char('_');
		}

		return name;
	}
} // namespace

NewTaskDialog::NewTaskDialog(QWidget *parent) : QDialog(parent) {
	setWindowTitle(tr("New Task"));
	setModal(true);

	auto *mainLayout = new QVBoxLayout(this);
	auto *grid = new QGridLayout();
	grid->setHorizontalSpacing(12);
	grid->setVerticalSpacing(12);

	titleEdit = new QLineEdit(this);
	fileEdit = new QLineEdit(this);
	fileEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[^\\\\/:*?\"<>|\\s]*")), this));
	fileEdit->setToolTip(tr("The file name is fixed once the task is created."));
	timeLimitEdit = new QSpinBox(this);
	timeLimitEdit->setRange(1, Settings::upperBoundForTimeLimit());
	timeLimitEdit->setSuffix(tr(" ms"));
	memoryLimitEdit = new QSpinBox(this);
	memoryLimitEdit->setRange(1, Settings::upperBoundForMemoryLimit());
	memoryLimitEdit->setSuffix(tr(" MiB"));

	grid->addWidget(new QLabel(tr("Problem Title"), this), 0, 0);
	grid->addWidget(titleEdit, 0, 1);
	grid->addWidget(new QLabel(tr("File Name"), this), 1, 0);
	grid->addWidget(fileEdit, 1, 1);
	grid->addWidget(new QLabel(tr("Time Limit"), this), 2, 0);
	grid->addWidget(timeLimitEdit, 2, 1);
	grid->addWidget(new QLabel(tr("Memory Limit"), this), 3, 0);
	grid->addWidget(memoryLimitEdit, 3, 1);
	mainLayout->addLayout(grid);

	auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	okButton = buttonBox->button(QDialogButtonBox::Ok);
	mainLayout->addWidget(buttonBox);

	connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(titleEdit, &QLineEdit::textChanged, this, &NewTaskDialog::syncFileName);
	connect(fileEdit, &QLineEdit::textEdited, this, &NewTaskDialog::markFileEdited);

	setMinimumWidth(360);
	updateOk();
}

void NewTaskDialog::setDefaults(const QString &suggestedTitle, int timeLimit, int memoryLimit) {
	fileEdited = false;
	titleEdit->setText(suggestedTitle);
	fileEdit->setText(toFileName(suggestedTitle));
	timeLimitEdit->setValue(qBound(1, timeLimit, Settings::upperBoundForTimeLimit()));
	memoryLimitEdit->setValue(qBound(1, memoryLimit, Settings::upperBoundForMemoryLimit()));
	titleEdit->selectAll();
	updateOk();
}

auto NewTaskDialog::getProblemTitle() const -> QString { return titleEdit->text().trimmed(); }

auto NewTaskDialog::getSourceFileName() const -> QString { return toFileName(fileEdit->text()); }

auto NewTaskDialog::getTimeLimit() const -> int { return timeLimitEdit->value(); }

auto NewTaskDialog::getMemoryLimit() const -> int { return memoryLimitEdit->value(); }

void NewTaskDialog::syncFileName() {
	// 用户已经自己填过文件名，就不再被标题覆盖。
	if (! fileEdited)
		fileEdit->setText(toFileName(titleEdit->text()));

	updateOk();
}

void NewTaskDialog::markFileEdited() { fileEdited = true; }

void NewTaskDialog::updateOk() {
	okButton->setEnabled(! titleEdit->text().trimmed().isEmpty() &&
	                     ! fileEdit->text().trimmed().isEmpty());
}
