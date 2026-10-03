/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "taskeditwidget.h"
#include "ui_taskeditwidget.h"
//
#include "base/compiler.h"
#include "base/settings.h"
#include "core/task.h"

TaskEditWidget::TaskEditWidget(QWidget *parent) : QWidget(parent), ui(new Ui::TaskEditWidget) {
	ui->setupUi(this);
	editTask = nullptr;
	ui->interactorPath->setFilters(QDir::Files);
	ui->interactorPath->setFileExtensions(QStringList{"h", "hpp"});
	ui->graderPath->setFilters(QDir::Files);
	ui->graderPath->setFileExtensions(QStringList{"cpp", "cc", "cxx"});
	comparisonModes << int(Task::IgnoreSpacesMode) << int(Task::TestlibSpecialJudgeMode);
	connect(this, &TaskEditWidget::dataPathChanged, ui->interactorPath, &FileLineEdit::refreshFileList);
	connect(this, &TaskEditWidget::dataPathChanged, ui->graderPath, &FileLineEdit::refreshFileList);
	// 换比赛日后，校验器（testlib）的候选 .exe 也要跟着换。
	connect(this, &TaskEditWidget::dataPathChanged, this, &TaskEditWidget::refreshGraderRoots);
	ui->sourceFileName->setValidator(new QRegularExpressionValidator(QRegularExpression("\\w+"), this));
	// 试题创建后文件名不再允许更改（只读），但保留正常外观以便查看/复制。
	ui->sourceFileName->setReadOnly(true);
	ui->inputFileName->setValidator(
	    new QRegularExpressionValidator(QRegularExpression(R"((\w+)(\.\w+)?)"), this));
	ui->outputFileName->setValidator(
	    new QRegularExpressionValidator(QRegularExpression(R"((\w+)(\.\w+)?)"), this));
	ui->interactorName->setValidator(
	    new QRegularExpressionValidator(QRegularExpression(R"((\w+)(\.\w+)?)"), this));
	ui->answerFileExtension->setValidator(new QRegularExpressionValidator(QRegularExpression("\\w+"), this));
	ui->sourceFilesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	ui->graderFilesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	// ui->interactionButton->setVisible(false); //rebuilding interaction, remove it temporarily
	connect(ui->problemTitle, &QLineEdit::textChanged, this, &TaskEditWidget::problemTitleChanged);
	connect(ui->traditionalButton, &QRadioButton::toggled, this, &TaskEditWidget::setToTraditional);
	connect(ui->answersOnlyButton, &QRadioButton::toggled, this, &TaskEditWidget::setToAnswersOnly);
	connect(ui->interactionButton, &QRadioButton::toggled, this, &TaskEditWidget::setToInteraction);
	connect(ui->sourceFileName, &QLineEdit::textChanged, this, &TaskEditWidget::sourceFileNameChanged);
	connect(ui->subFolderCheck, &QCheckBox::checkStateChanged, this, &TaskEditWidget::subFolderCheckChanged);
	connect(ui->inputFileName, &QLineEdit::textChanged, this, &TaskEditWidget::inputFileNameChanged);
	connect(ui->outputFileName, &QLineEdit::textChanged, this, &TaskEditWidget::outputFileNameChanged);
	connect(ui->standardInputCheck, &QCheckBox::checkStateChanged, this,
	        &TaskEditWidget::standardInputCheckChanged);
	connect(ui->standardOutputCheck, &QCheckBox::checkStateChanged, this,
	        &TaskEditWidget::standardOutputCheckChanged);
	connect(ui->comparisonMode, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        &TaskEditWidget::comparisonModeChanged);
	connect(ui->testlibSpecialJudge, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        &TaskEditWidget::specialJudgeChanged);
	connect(ui->interactorPath, &QLineEdit::textChanged, this, &TaskEditWidget::interactorChanged);
	connect(ui->interactorPath, &QLineEdit::editingFinished, this,
	        &TaskEditWidget::graderPathEditingFinished);
	connect(ui->graderPath, &QLineEdit::editingFinished, this, &TaskEditWidget::graderPathEditingFinished);
	connect(ui->interactorName, &QLineEdit::textChanged, this, &TaskEditWidget::interactorNameChanged);
	connect(ui->graderPath, &QLineEdit::textChanged, this, &TaskEditWidget::graderChanged);
	connect(ui->compilersList, &QListWidget::currentRowChanged, this,
	        &TaskEditWidget::compilerSelectionChanged);
	connect(ui->configurationSelect, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        &TaskEditWidget::configurationSelectionChanged);
	connect(ui->answerFileExtension, &QLineEdit::textChanged, this,
	        &TaskEditWidget::answerFileExtensionChanged);
	connect(ui->sourceFilesAppendButton, &QPushButton::clicked, this, &TaskEditWidget::addSourceFileClicked);
	connect(ui->graderFilesAppendButton, &QPushButton::clicked, this, &TaskEditWidget::addGraderFileClicked);
	connect(ui->sourceFilesRemoveButton, &QPushButton::clicked, this, &TaskEditWidget::rmSourceFileClicked);
	connect(ui->graderFilesRemoveButton, &QPushButton::clicked, this, &TaskEditWidget::rmGraderFileClicked);
}

TaskEditWidget::~TaskEditWidget() { delete ui; }

void TaskEditWidget::changeEvent(QEvent *event) {
	if (event->type() == QEvent::LanguageChange) {
		Task *bak = editTask;
		setEditTask(nullptr);
		ui->retranslateUi(this);
		setEditTask(bak);
	}
}

void TaskEditWidget::setEditTask(Task *task) {
	if (editTask) {
		disconnect(editTask, &Task::problemTitleChanged, this, &TaskEditWidget::refreshProblemTitle);
		disconnect(editTask, &Task::compilerConfigurationRefreshed, this,
		           &TaskEditWidget::refreshCompilerConfiguration);
	}

	editTask = task;

	if (! task)
		return;

	connect(editTask, &Task::problemTitleChanged, this, &TaskEditWidget::refreshProblemTitle);
	connect(editTask, &Task::compilerConfigurationRefreshed, this,
	        &TaskEditWidget::refreshCompilerConfiguration);
	ui->problemTitle->setText(editTask->getProblemTitle());
	// 文件名不可编辑，这里只是把值刷上去，不触发写回。
	loadingTask = true;
	ui->sourceFileName->setText(editTask->getSourceFileName());

	if (ui->sourceFileName->text().length() <= 0)
		ui->sourceFileName->setText(ui->problemTitle->text());

	loadingTask = false;
	ui->subFolderCheck->setChecked(editTask->getSubFolderCheck());
	ui->inputFileName->setText(editTask->getInputFileName());
	ui->outputFileName->setText(editTask->getOutputFileName());
	refreshComparisonMode(int(editTask->getComparisonMode()));
	// 校验器的可执行文件由 refreshGraderRoots() 列成下拉框并选中。
	ui->interactorPath->setText(editTask->getInteractor());
	ui->interactorName->setText(editTask->getInteractorName());
	ui->graderPath->setText(editTask->getGrader());
	// 交互库与主接口程序只能从 <题>/graders/ 中选取
	refreshGraderRoots();
	ui->standardInputCheck->setChecked(editTask->getStandardInputCheck());
	ui->standardOutputCheck->setChecked(editTask->getStandardOutputCheck());
	// ui->interactorPathLabel->setVisible(editTask->getTaskType() == Task::Interaction);
	// ui->interactorPath->setVisible(editTask->getTaskType() == Task::Interaction);
	// ui->graderPathLabel->setVisible(editTask->getTaskType() == Task::Interaction);
	// ui->graderPath->setVisible(editTask->getTaskType() == Task::Interaction);
	ui->answerFileExtension->setText(editTask->getAnswerFileExtension());
	refreshCompilerConfiguration();

	if (editTask->getTaskType() == Task::Traditional) {
		ui->traditionalButton->setChecked(true);
	}

	if (editTask->getTaskType() == Task::AnswersOnly) {
		ui->answersOnlyButton->setChecked(true);
	}

	if (editTask->getTaskType() == Task::Interaction) {
		ui->interactionButton->setChecked(true);
	}

	refreshWidgetState();
}

void TaskEditWidget::setSettings(Settings *_settings) { settings = _settings; }

void TaskEditWidget::refreshWidgetState() {
	if (! editTask)
		return;

	int types = editTask->getTaskType();
	ui->interactorPathLabel->setVisible(types == Task::Interaction);
	ui->interactorPath->setVisible(types == Task::Interaction);
	ui->graderPathLabel->setVisible(types == Task::Interaction);
	ui->graderPath->setVisible(types == Task::Interaction);
	ui->interactorNameLabel->setVisible(types == Task::Interaction);
	ui->interactorName->setVisible(types == Task::Interaction);
	// ui->comparisonSetting->setVisible(types != Task::Interaction);
	ui->sourceFileName->setEnabled(true);
	ui->sourceFileNameLabel->setEnabled(types == Task::Traditional || types == Task::Interaction ||
	                                    types == Task::AnswersOnly || types == Task::Communication ||
	                                    types == Task::CommunicationExec);
	ui->sourceFileName->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                               types == Task::AnswersOnly || types == Task::Communication ||
	                               types == Task::CommunicationExec);
	ui->sourceFileNameLabel->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                                    types == Task::AnswersOnly || types == Task::Communication ||
	                                    types == Task::CommunicationExec);
	ui->subFolderCheck->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                               types == Task::AnswersOnly || types == Task::Communication ||
	                               types == Task::CommunicationExec);
	ui->inputFileName->setEnabled((types == Task::Traditional || types == Task::Interaction ||
	                               types == Task::Communication || types == Task::CommunicationExec) &&
	                              ! editTask->getStandardInputCheck());
	ui->inputFileName->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                              types == Task::Communication || types == Task::CommunicationExec);
	ui->inputFileNameLabel->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                                   types == Task::Communication || types == Task::CommunicationExec);
	ui->standardInputCheck->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                                   types == Task::Communication || types == Task::CommunicationExec);
	ui->outputFileName->setEnabled((types == Task::Traditional || types == Task::Interaction ||
	                                types == Task::Communication || types == Task::CommunicationExec) &&
	                               ! editTask->getStandardOutputCheck());
	ui->outputFileName->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                               types == Task::Communication || types == Task::CommunicationExec);
	ui->outputFileNameLabel->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                                    types == Task::Communication || types == Task::CommunicationExec);
	ui->standardOutputCheck->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                                    types == Task::Communication || types == Task::CommunicationExec);
	ui->compilerSettingsLabel->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                                      types == Task::Communication || types == Task::CommunicationExec);
	ui->compilersList->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                              types == Task::Communication || types == Task::CommunicationExec);
	ui->configurationLabel->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                                   types == Task::Communication || types == Task::CommunicationExec);
	ui->configurationSelect->setVisible(types == Task::Traditional || types == Task::Interaction ||
	                                    types == Task::Communication || types == Task::CommunicationExec);
	// ui->comparisonMode->setEnabled(types == Task::Traditional || types == Task::AnswersOnly);
	ui->answerFileExtension->setVisible(types == Task::AnswersOnly);
	ui->answerFileExtensionLabel->setVisible(types == Task::AnswersOnly);
	refreshComparisonSettingPage();
	ui->sourceFilesLabel->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	ui->sourceFilesTable->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	ui->graderFilesLabel->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	ui->graderFilesTable->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	ui->sourceFilesAppendButton->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	ui->graderFilesAppendButton->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	ui->sourceFilesRemoveButton->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	ui->graderFilesRemoveButton->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	ui->multiFilesPathLineEdit->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	ui->multiFilesNameLineEdit->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	ui->multiFilesPathNameLabel->setVisible(types == Task::Communication || types == Task::CommunicationExec);
	multiFilesRefresh();
}

void TaskEditWidget::problemTitleChanged(const QString &text) {
	if (! editTask)
		return;

	editTask->setProblemTitle(text);
}

void TaskEditWidget::setToTraditional(bool check) {
	if (! check || ! editTask)
		return;

	editTask->setTaskType(Task::Traditional);
	// editTask->setStandardOutputCheck(false); //fix stdout not save
	// ui->standardOutputCheck->setCheckState(Qt::Unchecked);
	refreshWidgetState();
}

void TaskEditWidget::setToAnswersOnly(bool check) {
	if (! check || ! editTask)
		return;

	editTask->setTaskType(Task::AnswersOnly);
	// editTask->setStandardOutputCheck(false);
	// ui->standardOutputCheck->setCheckState(Qt::Unchecked);
	refreshWidgetState();
}

void TaskEditWidget::setToInteraction(bool check) {
	if (! check || ! editTask)
		return;

	editTask->setTaskType(Task::Interaction);
	// 交互题：自动生成 graders/<题>.h 与 <题>_grader.cpp 并填好三个交互参数。
	editTask->prepareInteraction();
	ui->interactorPath->setText(editTask->getInteractor());
	ui->interactorName->setText(editTask->getInteractorName());
	ui->graderPath->setText(editTask->getGrader());
	refreshGraderRoots();
	// editTask->setStandardOutputCheck(true);
	// ui->standardOutputCheck->setCheckState(Qt::Checked);
	refreshWidgetState();
}

void TaskEditWidget::sourceFileNameChanged(const QString &text) {
	if (! editTask || loadingTask)
		return;

	QString trueText = text;

	if (trueText.length() <= 0)
		trueText = ui->problemTitle->text();

	editTask->setSourceFileName(trueText);

	if (ui->inputFileName->isEnabled()) {
		ui->inputFileName->setText(trueText + "." + settings->getDefaultInputFileExtension());
	}

	if (ui->outputFileName->isEnabled()) {
		ui->outputFileName->setText(trueText + "." + settings->getDefaultOutputFileExtension());
	}
}

void TaskEditWidget::subFolderCheckChanged() {
	if (! editTask)
		return;

	bool check = ui->subFolderCheck->isChecked();
	editTask->setSubFolderCheck(check);
}

void TaskEditWidget::inputFileNameChanged(const QString &text) {
	if (! editTask)
		return;

	editTask->setInputFileName(text);
}

void TaskEditWidget::outputFileNameChanged(const QString &text) {
	if (! editTask)
		return;

	editTask->setOutputFileName(text);
}

void TaskEditWidget::standardInputCheckChanged() {
	if (! editTask)
		return;

	bool check = ui->standardInputCheck->isChecked();
	editTask->setStandardInputCheck(check);
	ui->inputFileName->setEnabled(! check);
}

void TaskEditWidget::standardOutputCheckChanged() {
	if (! editTask)
		return;

	bool check = ui->standardOutputCheck->isChecked();
	editTask->setStandardOutputCheck(check);
	ui->outputFileName->setEnabled(! check);
}

void TaskEditWidget::comparisonModeChanged() {
	if (! editTask)
		return;

	int index = ui->comparisonMode->currentIndex();

	if (index < 0 || index >= comparisonModes.size())
		return;

	editTask->setComparisonMode(Task::ComparisonMode(comparisonModes.at(index)));
	// 立刻切换「比较设置」那一行：选自定义校验器时要马上出现可执行文件的选择框。
	refreshComparisonSettingPage();
}

// 只有自定义校验器（testlib）模式才需要选校验器可执行文件。
void TaskEditWidget::refreshComparisonSettingPage() {
	if (! editTask)
		return;

	ui->comparisonSetting->setCurrentIndex(
	    editTask->getComparisonMode() == Task::TestlibSpecialJudgeMode ? 1 : 0);
}

void TaskEditWidget::refreshComparisonMode(int mode) {
	int index = comparisonModes.indexOf(mode);

	if (index < 0) {
		ui->comparisonMode->setToolTip(tr("The comparison mode of this task is not supported any more."));
		ui->comparisonMode->setCurrentIndex(-1);
		return;
	}

	ui->comparisonMode->setToolTip(QString());
	ui->comparisonMode->setCurrentIndex(index);
}

void TaskEditWidget::specialJudgeChanged() {
	if (! editTask)
		return;

	editTask->setSpecialJudge(ui->testlibSpecialJudge->currentData().toString());
}

void TaskEditWidget::interactorChanged(const QString &text) {
	if (! editTask)
		return;

	editTask->setInteractor(text);
}

// 交互库与主接口程序（grader.cpp）都只从 <题>/graders/ 读取：返回规范化后的相对路径，
// 本来就在该目录里的原样返回。
static QString toGradersPath(const QString &relative, const QString &taskName) {
	const QString prefix = QDir::fromNativeSeparators(Settings::gradersPath(taskName));

	if (QDir::fromNativeSeparators(relative).startsWith(prefix))
		return relative;

	return Settings::graderFilePath(taskName, relative);
}

// 交互库与主接口程序的候选列表都限定到 <题>/graders/；
// 旧工程里指向其它目录的路径则在 graders/ 下确有同名文件时归一到新位置。
void TaskEditWidget::refreshGraderRoots() {
	if (! editTask)
		return;

	const QString taskName = currentTaskName();

	if (taskName.isEmpty())
		return;

	ui->interactorPath->setRootDirectory(Settings::gradersPath(taskName));
	ui->graderPath->setRootDirectory(Settings::gradersPath(taskName));

	auto adopt = [&](const QString &stored, QLineEdit *edit) {
		if (stored.isEmpty())
			return;

		const QString normalized = toGradersPath(stored, taskName);

		if (normalized == stored)
			return;

		if (QFileInfo::exists(Settings::dataPath() + normalized))
			edit->setText(normalized);
	};

	adopt(editTask->getInteractor(), ui->interactorPath);
	adopt(editTask->getGrader(), ui->graderPath);

	// 自定义校验器（testlib）的可执行文件同样只从 <题>/graders/ 里取，而且只认 .exe：
	// 把候选直接列成下拉框，不再让人手填路径。
	{
		const QString stored = editTask->getSpecialJudge();
		QString normalized = stored;

		if (! stored.isEmpty()) {
			const QString candidate = toGradersPath(stored, taskName);

			if (candidate != stored && QFileInfo::exists(Settings::dataPath() + candidate))
				normalized = candidate;
		}

		QSignalBlocker blocker(ui->testlibSpecialJudge);
		ui->testlibSpecialJudge->clear();

		const QDir graders(Settings::dataPath() + Settings::gradersPath(taskName));

		// 可以直接选编译好的 .exe，也可以选校验器源码：评测前会先编译一次，
		// 之后（包括后续选手）都复用这个可执行文件。
		QStringList candidates =
		    graders.entryList({QStringLiteral("*.exe")}, QDir::Files, QDir::Name);
		candidates += graders.entryList(
		    {QStringLiteral("*.cpp"), QStringLiteral("*.cc"), QStringLiteral("*.cxx"), QStringLiteral("*.c")},
		    QDir::Files, QDir::Name);

		for (const QString &name : candidates)
			ui->testlibSpecialJudge->addItem(name, Settings::graderFilePath(taskName, name));

		const int target = ui->testlibSpecialJudge->findData(normalized);

		if (target >= 0) {
			ui->testlibSpecialJudge->setCurrentIndex(target);

			if (normalized != stored)
				editTask->setSpecialJudge(normalized);
		} else {
			// 候选里没有它就说明这个文件已经不存在了（或不在 graders/ 下）：不留这种
			// 点不出来的「幽灵」选项，直接把设置清掉，让用户重新选一个真实的 .exe。
			ui->testlibSpecialJudge->setCurrentIndex(-1);

			if (! stored.isEmpty())
				editTask->setSpecialJudge(QString());
		}
	}
}

// 手填的路径也只会去 <题>/graders/ 找，这里把控件内容一并纠正过去。
void TaskEditWidget::graderPathEditingFinished() {
	if (! editTask)
		return;

	const QString taskName = currentTaskName();

	if (taskName.isEmpty())
		return;

	const QList<QLineEdit *> edits = {ui->interactorPath, ui->graderPath};

	for (QLineEdit *edit : edits) {
		const QString text = edit->text();

		if (text.isEmpty())
			continue;

		const QString normalized = toGradersPath(text, taskName);

		if (normalized != text)
			edit->setText(normalized);
	}
}

auto TaskEditWidget::currentTaskName() const -> QString {
	if (! editTask)
		return {};

	const QString sourceFileName = editTask->getSourceFileName();
	return sourceFileName.isEmpty() ? editTask->getProblemTitle() : sourceFileName;
}

void TaskEditWidget::interactorNameChanged(const QString &text) {
	if (! editTask)
		return;

	editTask->setInteractorName(text);
}

void TaskEditWidget::graderChanged(const QString &text) {
	if (! editTask)
		return;

	editTask->setGrader(text);
}

void TaskEditWidget::refreshProblemTitle(const QString &title) {
	if (! editTask)
		return;

	ui->problemTitle->setText(title);
}

void TaskEditWidget::refreshCompilerConfiguration() {
	if (! editTask)
		return;

	ui->compilersList->setEnabled(false);
	ui->configurationSelect->setEnabled(false);
	ui->configurationLabel->setEnabled(false);
	ui->compilersList->clear();
	ui->configurationSelect->clear();
	const QList<Compiler *> &compilerList = settings->getCompilerList();

	if (compilerList.isEmpty())
		return;

	for (auto *i : compilerList) {
		ui->compilersList->addItem(i->getCompilerName());
	}

	ui->compilersList->setEnabled(true);
	ui->configurationSelect->setEnabled(true);
	ui->configurationLabel->setEnabled(true);
	ui->compilersList->setCurrentRow(0);
	compilerSelectionChanged();
}

void TaskEditWidget::compilerSelectionChanged() {
	if (! editTask)
		return;

	if (! ui->compilersList->isEnabled())
		return;

	ui->configurationSelect->setEnabled(false);
	ui->configurationSelect->clear();
	ui->configurationSelect->addItem("disable");
	const QList<Compiler *> &compilerList = settings->getCompilerList();

	for (auto *i : compilerList) {
		if (i->getCompilerName() == ui->compilersList->currentItem()->text()) {
			ui->configurationSelect->addItems(i->getConfigurationNames());
		}
	}

	QString config = editTask->getCompilerConfiguration(ui->compilersList->currentItem()->text());
	ui->configurationSelect->setCurrentIndex(ui->configurationSelect->findText(config));
	ui->configurationSelect->setEnabled(true);
}

void TaskEditWidget::configurationSelectionChanged() {
	if (! editTask)
		return;

	if (! ui->configurationSelect->isEnabled())
		return;

	editTask->setCompilerConfiguration(ui->compilersList->currentItem()->text(),
	                                   ui->configurationSelect->currentText());
}

void TaskEditWidget::answerFileExtensionChanged(const QString &extension) {
	if (! editTask)
		return;

	editTask->setAnswerFileExtension(extension);
}

void TaskEditWidget::multiFilesRefresh() {
	if (! editTask)
		return;

	if (editTask->getTaskType() != Task::Communication && editTask->getTaskType() != Task::CommunicationExec)
		return;

	QStringList sourcePaths = editTask->getSourceFilesPath();
	QStringList sourceNames = editTask->getSourceFilesName();
	ui->sourceFilesTable->setRowCount(sourcePaths.length());

	for (int i = 0; i < sourcePaths.length(); i++) {
		ui->sourceFilesTable->setItem(i, 0, new QTableWidgetItem(sourcePaths[i]));
		ui->sourceFilesTable->setItem(i, 1, new QTableWidgetItem(sourceNames[i]));
	}

	QStringList graderPaths = editTask->getGraderFilesPath();
	QStringList graderNames = editTask->getGraderFilesName();
	ui->graderFilesTable->setRowCount(graderPaths.length());

	for (int i = 0; i < graderPaths.length(); i++) {
		ui->graderFilesTable->setItem(i, 0, new QTableWidgetItem(graderPaths[i]));
		ui->graderFilesTable->setItem(i, 1, new QTableWidgetItem(graderNames[i]));
	}
}

void TaskEditWidget::addSourceFiles(const QString &path, const QString &name) {
	if (! editTask)
		return;

	editTask->appendSourceFiles(path, name);
}

void TaskEditWidget::rmSourceFilesAt(int loca) {
	if (! editTask)
		return;

	editTask->removeSourceFilesAt(loca);
}

void TaskEditWidget::rmGraderFilesAt(int loca) {
	if (! editTask)
		return;

	editTask->removeGraderFilesAt(loca);
}

void TaskEditWidget::addGraderFiles(const QString &path, const QString &name) {
	if (! editTask)
		return;

	editTask->appendGraderFiles(path, name);
}

void TaskEditWidget::addSourceFileClicked() {
	if (! editTask)
		return;

	QString path = ui->multiFilesPathLineEdit->text();
	QString name = ui->multiFilesNameLineEdit->text();

	if (path.length() <= 0 || name.length() <= 0)
		return;

	addSourceFiles(path, name);
	ui->multiFilesPathLineEdit->clear();
	ui->multiFilesNameLineEdit->clear();
	multiFilesRefresh();
}

void TaskEditWidget::addGraderFileClicked() {
	if (! editTask)
		return;

	QString path = ui->multiFilesPathLineEdit->text();
	QString name = ui->multiFilesNameLineEdit->text();

	if (path.length() <= 0 || name.length() <= 0)
		return;

	addGraderFiles(path, name);
	ui->multiFilesPathLineEdit->clear();
	ui->multiFilesNameLineEdit->clear();
	multiFilesRefresh();
}

void TaskEditWidget::rmSourceFileClicked() {
	if (! editTask)
		return;

	QList<QTableWidgetSelectionRange> ranges = ui->sourceFilesTable->selectedRanges();

	if (ranges.length() <= 0)
		return;

	rmSourceFilesAt(ranges.at(0).topRow());
	multiFilesRefresh();
}

void TaskEditWidget::rmGraderFileClicked() {
	if (! editTask)
		return;

	QList<QTableWidgetSelectionRange> ranges = ui->graderFilesTable->selectedRanges();

	if (ranges.length() <= 0)
		return;

	rmGraderFilesAt(ranges.at(0).topRow());
	multiFilesRefresh();
}
