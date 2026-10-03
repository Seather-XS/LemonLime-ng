/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "addtaskdialog.h"
#include "ui_addtaskdialog.h"
//
#include "base/settings.h"
//
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QSet>
//
#include <algorithm>

namespace {
	/// 在题目录及其 graders/ 子目录里按名字过滤找文件（返回 显示名, 绝对路径）。
	QList<QPair<QString, QString>> candidateFiles(const QString &taskDir, const QStringList &nameFilters) {
		QList<QPair<QString, QString>> found;
		QSet<QString> seen;
		const QStringList dirs = {taskDir, taskDir + QDir::separator() + QStringLiteral("graders")};

		for (const QString &dir : dirs) {
			const QFileInfoList entries = QDir(dir).entryInfoList(nameFilters, QDir::Files);

			for (const QFileInfo &info : entries) {
				const QString path = info.absoluteFilePath();

				if (seen.contains(path))
					continue;

				seen.insert(path);
				found.append({info.fileName(), path});
			}
		}

		std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		return found;
	}

	/// 把下拉框切到 stored 对应的项；存的那项没了（或本来就没存）就退回第一项，
	/// 并把最终选择写回 stored。
	void applySelection(QComboBox *combo, QString &stored) {
		if (combo->count() == 0) {
			stored.clear();
			return;
		}

		int target = combo->findData(stored);

		if (target < 0)
			target = 0;

		combo->setCurrentIndex(target);
		stored = combo->itemData(target).toString();
	}
} // namespace

AddTaskDialog::AddTaskDialog(QWidget *parent) : QDialog(parent), ui(new Ui::AddTaskDialog) {
	ui->setupUi(this);
	ui->fullScore->setValidator(new QIntValidator(1, Settings::upperBoundForFullScore() * 100, this));
	ui->timeLimit->setValidator(new QIntValidator(1, Settings::upperBoundForTimeLimit(), this));
	ui->memoryLimit->setValidator(new QIntValidator(1, Settings::upperBoundForMemoryLimit(), this));
	connect(ui->taskBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        &AddTaskDialog::taskBoxIndexChanged);
	connect(ui->fullScore, &QLineEdit::textChanged, this, &AddTaskDialog::fullScoreChanged);
	connect(ui->timeLimit, &QLineEdit::textChanged, this, &AddTaskDialog::timeLimitChanged);
	connect(ui->memoryLimit, &QLineEdit::textChanged, this, &AddTaskDialog::memoryLimitChanged);

	// 题型：与「试题」选项卡里的三个选项一致。
	ui->taskTypeBox->addItem(tr("Traditional"), int(Task::Traditional));
	ui->taskTypeBox->addItem(tr("Answers Only"), int(Task::AnswersOnly));
	ui->taskTypeBox->addItem(tr("Interaction"), int(Task::Interaction));
	connect(ui->taskTypeBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        &AddTaskDialog::taskTypeChanged);
	connect(ui->interactorBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        &AddTaskDialog::sourceSelectionChanged);
	connect(ui->graderBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        &AddTaskDialog::sourceSelectionChanged);
	connect(ui->checkerBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        &AddTaskDialog::sourceSelectionChanged);
	ui->checkerBox->setToolTip(
	    tr("Only .cpp files can be the checker; it is compiled once before judging."));

	// 判题方式：与「试题」选项卡里的两项一致。
	ui->comparisonModeBox->addItem(tr("Line-by-line mode (ignore extra spaces and tabs)"),
	                               int(Task::IgnoreSpacesMode));
	ui->comparisonModeBox->addItem(tr("Special judge mode (testlib)"), int(Task::TestlibSpecialJudgeMode));
	connect(ui->comparisonModeBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        &AddTaskDialog::comparisonChanged);

	updateOptionalRows();
}

AddTaskDialog::~AddTaskDialog() { delete ui; }

void AddTaskDialog::addTask(const QString &title, int _fullScore, int _timeLimit, int _memoryLimit) {
	fullScore.append(_fullScore);
	timeLimit.append(_timeLimit);
	memoryLimit.append(_memoryLimit);
	taskTypes.append(Task::Traditional);
	interactorSources.append(QString());
	graderSources.append(QString());
	comparisonChoices.append(Task::IgnoreSpacesMode);
	checkerSources.append(QString());
	ui->taskBox->addItem(title);
	ui->taskBox->setCurrentIndex(0);
}

void AddTaskDialog::setSourceRoot(const QString &root) { sourceRoot = root; }

void AddTaskDialog::taskBoxIndexChanged() {
	const int index = ui->taskBox->currentIndex();

	if (index < 0 || index >= taskTypes.size())
		return;

	// 载入这一道题的设置，期间控件的变化不要回写。
	loading = true;
	ui->fullScore->setText(QString::number(fullScore[index]));
	ui->timeLimit->setText(QString::number(timeLimit[index]));
	ui->memoryLimit->setText(QString::number(memoryLimit[index]));
	ui->taskTypeBox->setCurrentIndex(ui->taskTypeBox->findData(int(taskTypes[index])));
	ui->comparisonModeBox->setCurrentIndex(ui->comparisonModeBox->findData(int(comparisonChoices[index])));
	refreshCandidates();
	loading = false;

	updateOptionalRows();
}

// 交互库 / 接口 / 校验器的候选：这道题在导入目录下的对应文件（含 graders/ 子目录）。
void AddTaskDialog::refreshCandidates() {
	const int index = ui->taskBox->currentIndex();

	if (index < 0 || index >= taskTypes.size())
		return;

	const QString taskDir = sourceRoot + QDir::separator() + ui->taskBox->currentText();

	ui->interactorBox->clear();
	ui->graderBox->clear();

	for (const auto &file : candidateFiles(taskDir, {QStringLiteral("*.h"), QStringLiteral("*.hpp")}))
		ui->interactorBox->addItem(file.first, file.second);

	for (const auto &file : candidateFiles(taskDir, {QStringLiteral("*.cpp")}))
		ui->graderBox->addItem(file.first, file.second);

	// 校验器：只能是 .cpp 源码，评测前会编译一次并复用编译结果。
	ui->checkerBox->clear();

	for (const auto &file : candidateFiles(taskDir, {QStringLiteral("*.cpp")}))
		ui->checkerBox->addItem(file.first, file.second);

	// 一个 .cpp 都没有时给个提示项，免得导入完才发现校验器没选上。
	ui->checkerBox->setEnabled(ui->checkerBox->count() > 0);

	if (ui->checkerBox->count() == 0)
		ui->checkerBox->addItem(tr("No .cpp checker found"), QString());

	applySelection(ui->interactorBox, interactorSources[index]);
	applySelection(ui->graderBox, graderSources[index]);
	applySelection(ui->checkerBox, checkerSources[index]);
}

// 交互题才要指定交互库/接口，自定义校验器才要指定校验器，其余两行藏起来。
void AddTaskDialog::updateOptionalRows() {
	const bool interaction = Task::TaskType(ui->taskTypeBox->currentData().toInt()) == Task::Interaction;

	ui->interactorLabel->setVisible(interaction);
	ui->interactorBox->setVisible(interaction);
	ui->graderLabel->setVisible(interaction);
	ui->graderBox->setVisible(interaction);

	const bool custom = Task::ComparisonMode(ui->comparisonModeBox->currentData().toInt()) ==
	                    Task::TestlibSpecialJudgeMode;

	ui->checkerPathLabel->setVisible(custom);
	ui->checkerBox->setVisible(custom);

	if (isVisible())
		adjustSize();
}

auto AddTaskDialog::getFullScore(int index) const -> int {
	if (0 <= index && index < fullScore.size()) {
		return fullScore[index];
	}

	return 0;
}

auto AddTaskDialog::getTimeLimit(int index) const -> int {
	if (0 <= index && index < timeLimit.size()) {
		return timeLimit[index];
	}

	return 0;
}

auto AddTaskDialog::getMemoryLimit(int index) const -> int {
	if (0 <= index && index < memoryLimit.size()) {
		return memoryLimit[index];
	}

	return 0;
}

auto AddTaskDialog::getTaskType(int index) const -> Task::TaskType {
	if (0 <= index && index < taskTypes.size())
		return taskTypes[index];

	return Task::Traditional;
}

auto AddTaskDialog::getInteractorSource(int index) const -> QString {
	if (0 <= index && index < interactorSources.size())
		return interactorSources[index];

	return {};
}

auto AddTaskDialog::getGraderSource(int index) const -> QString {
	if (0 <= index && index < graderSources.size())
		return graderSources[index];

	return {};
}

auto AddTaskDialog::getComparisonMode(int index) const -> Task::ComparisonMode {
	if (0 <= index && index < comparisonChoices.size())
		return comparisonChoices[index];

	return Task::IgnoreSpacesMode;
}

auto AddTaskDialog::getCheckerSource(int index) const -> QString {
	if (0 <= index && index < checkerSources.size())
		return checkerSources[index];

	return {};
}

void AddTaskDialog::fullScoreChanged() {
	int index = ui->taskBox->currentIndex();

	if (index < 0 || index >= fullScore.size())
		return;

	fullScore[index] = ui->fullScore->text().toInt();
}

void AddTaskDialog::timeLimitChanged() {
	int index = ui->taskBox->currentIndex();

	if (index < 0 || index >= timeLimit.size())
		return;

	timeLimit[index] = ui->timeLimit->text().toInt();
}

void AddTaskDialog::memoryLimitChanged() {
	int index = ui->taskBox->currentIndex();

	if (index < 0 || index >= memoryLimit.size())
		return;

	memoryLimit[index] = ui->memoryLimit->text().toInt();
}

void AddTaskDialog::taskTypeChanged() {
	if (! loading) {
		const int index = ui->taskBox->currentIndex();

		if (index >= 0 && index < taskTypes.size())
			taskTypes[index] = Task::TaskType(ui->taskTypeBox->currentData().toInt());
	}

	updateOptionalRows();
}

void AddTaskDialog::comparisonChanged() {
	if (! loading) {
		const int index = ui->taskBox->currentIndex();

		if (index >= 0 && index < comparisonChoices.size())
			comparisonChoices[index] =
			    Task::ComparisonMode(ui->comparisonModeBox->currentData().toInt());
	}

	updateOptionalRows();
}

void AddTaskDialog::sourceSelectionChanged() {
	if (loading)
		return;

	const int index = ui->taskBox->currentIndex();

	if (index < 0 || index >= interactorSources.size())
		return;

	interactorSources[index] = ui->interactorBox->currentData().toString();
	graderSources[index] = ui->graderBox->currentData().toString();
	checkerSources[index] = ui->checkerBox->currentData().toString();
}
