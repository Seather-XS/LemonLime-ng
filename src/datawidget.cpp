/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "datawidget.h"
#include "base/compiler.h"
#include "base/settings.h"
#include "core/contest.h"
#include "core/task.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
	constexpr int runTimeLimit = 120 * 1000;

	QString summarizeNames(const QString &shortFormat, const QString &longFormat, const QStringList &names) {
		if (names.isEmpty())
			return {};

		if (names.size() <= 3)
			return shortFormat.arg(names.join(QStringLiteral(", ")));

		return longFormat.arg(names.first(), names.last()).arg(names.size());
	}

	Compiler *pickCompiler(Settings *settings) {
		if (! settings)
			return nullptr;

		Compiler *fallback = nullptr;

		for (Compiler *compiler : settings->getCompilerList()) {
			if (! compiler || compiler->getCompilerType() != Compiler::Typical)
				continue;

			if (! compiler->getSourceExtensions().contains(QStringLiteral("cpp")))
				continue;

			if (compiler->getCompilerName().compare(QStringLiteral("g++"), Qt::CaseInsensitive) == 0)
				return compiler;

			if (! fallback)
				fallback = compiler;
		}

		return fallback;
	}
}

DataWidget::DataWidget(QWidget *parent) : QWidget(parent) {
	generator = new DataGenerator(this);
	buildUi();
	connect(generator, &DataGenerator::logMessage, this, &DataWidget::appendLog);
	connect(generator, &DataGenerator::statusMessage, this, [this](const QString &text) {
		setStatus(text);
		emit statusChanged(text);
	});

	configTimer = new QTimer(this);
	configTimer->setSingleShot(true);
	configTimer->setInterval(400);
	connect(configTimer, &QTimer::timeout, this, &DataWidget::saveConfig);
}

DataWidget::~DataWidget() {
	if (generateThread) {
		generateThread->wait();
		delete generateThread;
	}
}

void DataWidget::changeEvent(QEvent *event) {
	if (event->type() == QEvent::LanguageChange)
		retranslate();

	QWidget::changeEvent(event);
}

void DataWidget::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	refresh();
}

void DataWidget::buildUi() {
	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	auto *listBox = new QGroupBox(tr("Tasks"), this);
	listBox->setMinimumWidth(176);
	auto *listLayout = new QVBoxLayout(listBox);
	taskList = new QListWidget(listBox);
	listLayout->addWidget(taskList);

	auto *right = new QWidget(this);
	auto *rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(0, 0, 0, 0);

	auto *settingsBox = new QGroupBox(tr("Generator Settings"), right);
	auto *form = new QFormLayout(settingsBox);

	patternEdit = new QLineEdit(QStringLiteral("<name><testid>"), settingsBox);
	patternEdit->setToolTip(tr("Placeholders: <name> = task file name, <testid> = test point number, "
	                           "<subid> = subtask number (needs \"Name Files by Subtask\"). "
	                           "<testid> is zero-padded to two digits when naming by subtask. "
	                           "Sample names never use <subid>: it is dropped, and the generator still "
	                           "gets sub_id -1."));
	form->addRow(tr("File Name Prefix"), patternEdit);

	subtaskBox = new QCheckBox(tr("Configure by Subtask"), settingsBox);
	subtaskBox->setToolTip(tr("One test point per subtask; a later subtask only scores when the earlier ones pass."));
	form->addRow(QString(), subtaskBox);

	caseSpin = new QSpinBox(settingsBox);
	caseSpin->setRange(1, 1000);
	caseSpin->setValue(10);
	form->addRow(tr("Test Cases"), caseSpin);

	subtaskLabel = new QLabel(tr("Test Points per Subtask"), settingsBox);
	subtaskEdit = new QLineEdit(settingsBox);
	subtaskEdit->setPlaceholderText(QStringLiteral("5 5 4 6"));
	subtaskEdit->setToolTip(tr("Number of test points in each subtask, separated by spaces."));
	form->addRow(subtaskLabel, subtaskEdit);

	subtaskNamingBox = new QCheckBox(tr("Name Files by Subtask"), settingsBox);
	subtaskNamingBox->setToolTip(tr("\"<subid>\" becomes usable and \"<testid>\" counts within each subtask. "
	                                "Otherwise the test points are numbered in generation order."));
	form->addRow(QString(), subtaskNamingBox);

	sampleSpin = new QSpinBox(settingsBox);
	sampleSpin->setRange(0, 1000);
	sampleSpin->setValue(0);
	sampleSpin->setToolTip(tr("Samples are generated with sub_id -1 and written to down/."));
	form->addRow(tr("Samples"), sampleSpin);

	inputSuffixEdit = new QLineEdit(QStringLiteral(".in"), settingsBox);
	form->addRow(tr("Input Suffix"), inputSuffixEdit);
	outputSuffixEdit = new QLineEdit(QStringLiteral(".ans"), settingsBox);
	form->addRow(tr("Output Suffix"), outputSuffixEdit);
	sampleSuffixEdit = new QLineEdit(QStringLiteral(".ans"), settingsBox);
	form->addRow(tr("Sample Output Suffix"), sampleSuffixEdit);

	cleanBox = new QCheckBox(tr("Remove the old files in data/ and down/ first"), settingsBox);
	cleanBox->setChecked(true);
	form->addRow(QString(), cleanBox);

	configureBox = new QCheckBox(tr("Write the result into the test cases of this task"), settingsBox);
	configureBox->setChecked(true);
	form->addRow(QString(), configureBox);

	rightLayout->addWidget(settingsBox);

	previewLabel = new QLabel(right);
	previewLabel->setWordWrap(true);
	previewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	previewLabel->setStyleSheet(QStringLiteral("color: gray;"));
	rightLayout->addWidget(previewLabel);

	auto *buttonBar = new QHBoxLayout();
	generateButton = new QPushButton(tr("Generate Data"), right);
	generateButton->setToolTip(tr("Compile generator/gen.cpp + generator/sol.cpp, run them, and configure "
	                              "the test cases."));
	openFolderButton = new QPushButton(tr("Open generator Folder"), right);
	editGeneratorButton = new QPushButton(tr("Edit gen.cpp"), right);
	editGeneratorButton->setToolTip(tr("Open gen.cpp with the default editor (it is created from a template "
	                                   "when missing)."));
	editSolutionButton = new QPushButton(tr("Edit sol.cpp"), right);
	editSolutionButton->setToolTip(tr("Open sol.cpp with the default editor (a skeleton is created when missing)."));
	buttonBar->addWidget(generateButton);
	buttonBar->addWidget(openFolderButton);
	buttonBar->addWidget(editGeneratorButton);
	buttonBar->addWidget(editSolutionButton);
	buttonBar->addStretch();
	statusLabel = new QLabel(right);
	buttonBar->addWidget(statusLabel);
	rightLayout->addLayout(buttonBar);

	auto *logBox = new QGroupBox(tr("Log"), right);
	auto *logLayout = new QVBoxLayout(logBox);
	logView = new QPlainTextEdit(logBox);
	logView->setReadOnly(true);
	logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	logLayout->addWidget(logView);
	rightLayout->addWidget(logBox, 1);

	layout->addWidget(listBox, 1);
	layout->addWidget(right, 3);

	connect(taskList, &QListWidget::currentRowChanged, this, &DataWidget::taskSelectionChanged);
	connect(subtaskBox, &QCheckBox::toggled, this, &DataWidget::updateParameterState);
	connect(subtaskNamingBox, &QCheckBox::toggled, this, &DataWidget::updateParameterState);
	connect(caseSpin, &QSpinBox::valueChanged, this, &DataWidget::updateParameterState);
	connect(sampleSpin, &QSpinBox::valueChanged, this, &DataWidget::updateParameterState);
	connect(subtaskEdit, &QLineEdit::textChanged, this, &DataWidget::updateParameterState);
	connect(patternEdit, &QLineEdit::textChanged, this, &DataWidget::updateParameterState);
	connect(inputSuffixEdit, &QLineEdit::textChanged, this, &DataWidget::updateParameterState);
	connect(generateButton, &QPushButton::clicked, this, &DataWidget::generateClicked);
	connect(openFolderButton, &QPushButton::clicked, this, &DataWidget::openGeneratorFolder);
	connect(editGeneratorButton, &QPushButton::clicked, this, &DataWidget::openGeneratorFile);
	connect(editSolutionButton, &QPushButton::clicked, this, &DataWidget::openSolutionFile);

	retranslate();
	updateParameterState();
}

void DataWidget::retranslate() {
	if (! generateButton)
		return;

	generateButton->setText(tr("Generate Data"));
	openFolderButton->setText(tr("Open generator Folder"));
	editGeneratorButton->setText(tr("Edit gen.cpp"));
	editSolutionButton->setText(tr("Edit sol.cpp"));
	updateParameterState();
}

void DataWidget::setContest(Contest *contest) {
	flushConfig();
	configTask.clear();
	curContest = contest;
	refresh();
}

void DataWidget::setSettings(Settings *settings_) { settings = settings_; }

void DataWidget::refresh() {
	if (! taskList)
		return;

	const QString selected = taskList->currentItem() ? taskList->currentItem()->data(Qt::UserRole).toString()
	                                                 : QString();
	const QSignalBlocker blocker(taskList);
	taskList->clear();

	if (curContest) {
		for (Task *task : curContest->getTaskList()) {
			DataGenerator::ensureGeneratorTemplate(Settings::dataPath() + task->getDirectoryName());

			auto *item = new QListWidgetItem(task->getProblemTitle(), taskList);
			item->setData(Qt::UserRole, task->getDirectoryName());
			item->setToolTip(QDir::toNativeSeparators(Settings::dataPath() + task->getDirectoryName()));
		}
	}

	if (! selected.isEmpty()) {
		for (int row = 0; row < taskList->count(); row++)
			if (taskList->item(row)->data(Qt::UserRole).toString() == selected)
				taskList->setCurrentRow(row);
	}

	if (taskList->currentRow() < 0 && taskList->count() > 0)
		taskList->setCurrentRow(0);

	const bool hasTask = currentTask() != nullptr;
	generateButton->setEnabled(hasTask);
	openFolderButton->setEnabled(hasTask);
	editGeneratorButton->setEnabled(hasTask);
	editSolutionButton->setEnabled(hasTask);
	taskSelectionChanged();
}

Task *DataWidget::currentTask() const {
	if (! curContest || ! taskList || taskList->currentRow() < 0)
		return nullptr;

	const QString directory = taskList->currentItem()->data(Qt::UserRole).toString();

	for (Task *task : curContest->getTaskList())
		if (task->getDirectoryName() == directory)
			return task;

	return nullptr;
}

void DataWidget::taskSelectionChanged() {
	if (loadingConfig)
		return;

	flushConfig();

	Task *task = currentTask();
	configTask = task ? task->getDirectoryName() : QString();
	loadConfigFor(task);
	updateParameterState();
}

void DataWidget::updateParameterState() {
	if (! subtaskEdit)
		return;

	{
		const QSignalBlocker blocker(subtaskNamingBox);
		const bool subtask = subtaskBox->isChecked();
		caseSpin->setEnabled(! subtask);
		subtaskEdit->setEnabled(subtask);
		subtaskLabel->setEnabled(subtask);
		subtaskNamingBox->setEnabled(subtask);

		if (! subtask)
			subtaskNamingBox->setChecked(false);
	}

	Task *task = currentTask();
	generateButton->setEnabled(task != nullptr);
	openFolderButton->setEnabled(task != nullptr);
	editGeneratorButton->setEnabled(task != nullptr);
	editSolutionButton->setEnabled(task != nullptr);

	if (! task) {
		previewLabel->setText(tr("There is no task in this contest day yet."));
		return;
	}

	const DataGenerator::Options options = uiOptions();
	QString error;
	const QList<DataGenerator::PlannedTest> tests = DataGenerator::planTests(options, task->getDirectoryName(), &error);

	if (tests.isEmpty()) {
		previewLabel->setText(error.isEmpty() ? QString() : error);
		scheduleSaveConfig();
		return;
	}

	QStringList testNames;
	QStringList sampleNames;

	for (const auto &test : tests) {
		if (test.sample)
			sampleNames.append(test.fileName + options.inputSuffix);
		else
			testNames.append(test.fileName + options.inputSuffix);
	}

	QString text = summarizeNames(tr("Will generate %1 in data/."),
	                              tr("Will generate %1 … %2 in data/ (%3 files)."), testNames);

	if (! sampleNames.isEmpty())
		text += QStringLiteral("  ") +
		        summarizeNames(tr("Samples: %1 in down/."),
		                       tr("Samples: %1 … %2 in down/ (%3 files)."), sampleNames);

	if (options.subtaskMode) {
		QStringList counts;

		for (int count : options.subtaskCases)
			counts.append(QString::number(count));

		text += QStringLiteral("  ") + tr("Subtasks: %1.").arg(counts.join(QStringLiteral(" / ")));
	}

	previewLabel->setText(text);
	scheduleSaveConfig();
}

DataGenerator::Options DataWidget::uiOptions() const {
	DataGenerator::Options options;

	if (! patternEdit)
		return options;

	options.namePattern = patternEdit->text().trimmed();
	options.inputSuffix = inputSuffixEdit->text().trimmed();
	options.outputSuffix = outputSuffixEdit->text().trimmed();
	options.sampleSuffix = sampleSuffixEdit->text().trimmed();
	options.subtaskMode = subtaskBox->isChecked();
	options.caseCount = caseSpin->value();
	options.sampleCount = sampleSpin->value();
	options.subtaskNaming = subtaskNamingBox->isChecked();
	options.cleanData = cleanBox->isChecked();
	options.configureTestCases = configureBox->isChecked();

	for (const QString &text : subtaskEdit->text().split(QRegularExpression(QStringLiteral("[\\s,]+")),
	                                                    Qt::SkipEmptyParts)) {
		bool ok = false;
		const int count = text.toInt(&ok);

		if (ok && count > 0)
			options.subtaskCases.append(count);
	}

	return options;
}

void DataWidget::loadConfigFor(Task *task) {
	DataGenerator::Options options;

	if (task)
		DataGenerator::loadConfig(Settings::dataPath() + task->getDirectoryName(), &options);

	loadingConfig = true;
	const QSignalBlocker patternBlocker(patternEdit);
	const QSignalBlocker subtaskBlocker(subtaskBox);
	const QSignalBlocker caseBlocker(caseSpin);
	const QSignalBlocker namingBlocker(subtaskNamingBox);
	const QSignalBlocker sampleBlocker(sampleSpin);
	const QSignalBlocker inputBlocker(inputSuffixEdit);
	const QSignalBlocker outputBlocker(outputSuffixEdit);
	const QSignalBlocker suffixBlocker(sampleSuffixEdit);
	const QSignalBlocker cleanBlocker(cleanBox);
	const QSignalBlocker configureBlocker(configureBox);

	patternEdit->setText(options.namePattern);
	inputSuffixEdit->setText(options.inputSuffix);
	outputSuffixEdit->setText(options.outputSuffix);
	sampleSuffixEdit->setText(options.sampleSuffix);
	subtaskBox->setChecked(options.subtaskMode);
	subtaskNamingBox->setChecked(options.subtaskNaming);
	caseSpin->setValue(options.caseCount);
	sampleSpin->setValue(options.sampleCount);
	cleanBox->setChecked(options.cleanData);
	configureBox->setChecked(options.configureTestCases);

	QStringList counts;

	for (int count : options.subtaskCases)
		counts.append(QString::number(count));

	subtaskEdit->setText(counts.join(QChar(' ')));
	loadingConfig = false;
}

void DataWidget::flushConfig() {
	if (configTimer)
		configTimer->stop();

	if (configTask.isEmpty() || loadingConfig)
		return;

	DataGenerator::saveConfig(Settings::dataPath() + configTask, uiOptions());
}

void DataWidget::scheduleSaveConfig() {
	if (loadingConfig || configTask.isEmpty() || ! configTimer)
		return;

	configTimer->start();
}

void DataWidget::saveConfig() {
	if (configTask.isEmpty() || loadingConfig)
		return;

	DataGenerator::saveConfig(Settings::dataPath() + configTask, uiOptions());
}

bool DataWidget::collectOptions(Task *task, DataGenerator::Options *options, QString *error) {
	Compiler *compiler = pickCompiler(settings);

	if (! compiler) {
		*error = tr("No C++ compiler is configured. Add one in Settings → Compilers first.");
		return false;
	}

	const QStringList configurations = compiler->getCompilerArguments();

	if (configurations.isEmpty()) {
		*error = tr("The compiler \"%1\" has no configuration.").arg(compiler->getCompilerName());
		return false;
	}

	int configuration = 0;
	const QString wanted = task->getCompilerConfiguration(compiler->getCompilerName());

	if (! wanted.isEmpty()) {
		const int index = compiler->getConfigurationNames().indexOf(wanted);

		if (index >= 0)
			configuration = index;
	}

	*options = uiOptions();
	options->taskDirectory = Settings::dataPath() + task->getDirectoryName();
	options->compilerLocation = compiler->getCompilerLocation();
	options->compilerArguments = configurations.at(configuration);
	options->environment = compiler->getEnvironment();
	options->compileTimeLimit = settings ? settings->getCompileTimeLimit() : 10000;
	options->runTimeLimit = runTimeLimit;
	options->configureTestCases = configureBox->isChecked();

	if (options->taskDirectory.isEmpty()) {
		*error = tr("No task selected.");
		return false;
	}

	if (options->inputSuffix.isEmpty() || options->outputSuffix.isEmpty() || options->sampleSuffix.isEmpty()) {
		*error = tr("The input / output suffixes cannot be empty.");
		return false;
	}

	if (options->subtaskMode) {
		for (const QString &text : subtaskEdit->text().split(
		         QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
			bool ok = false;
			const int count = text.toInt(&ok);

			if (! ok || count <= 0) {
				*error = tr("The test points per subtask must be positive integers, e.g. \"5 5 4 6\".");
				return false;
			}
		}

		if (options->subtaskCases.isEmpty()) {
			*error = tr("Fill in the test points per subtask, e.g. \"5 5 4 6\".");
			return false;
		}
	}

	QString planError;

	if (DataGenerator::planTests(*options, task->getDirectoryName(), &planError).isEmpty()) {
		*error = planError.isEmpty() ? tr("Cannot plan the test data.") : planError;
		return false;
	}

	return true;
}

void DataWidget::generateClicked() {
	if (generateThread)
		return;

	Task *task = currentTask();

	if (! task) {
		QMessageBox::information(this, tr("Generate Data"), tr("There is no task in this contest day yet."));
		return;
	}

	QString error;

	if (! collectOptions(task, &pendingOptions, &error)) {
		appendLog(error);
		QMessageBox::warning(this, tr("Generate Data"), error);
		return;
	}

	generateOk = false;
	pendingResult = DataGenerator::Result();
	logView->clear();
	generateButton->setEnabled(false);
	openFolderButton->setEnabled(false);
	editGeneratorButton->setEnabled(false);
	editSolutionButton->setEnabled(false);
	setStatus(tr("Generating ..."));
	emit statusChanged(tr("Generating ..."));
	appendLog(tr("Task: %1").arg(task->getProblemTitle()));

	generateThread = QThread::create([this] { generateOk = generator->run(pendingOptions, &pendingResult); });
	connect(generateThread, &QThread::finished, this, &DataWidget::generateFinished);
	generateThread->start();
}

void DataWidget::generateFinished() {
	QThread *thread = generateThread;
	generateThread = nullptr;

	if (thread)
		thread->deleteLater();

	generateButton->setEnabled(currentTask() != nullptr);
	openFolderButton->setEnabled(currentTask() != nullptr);
	editGeneratorButton->setEnabled(currentTask() != nullptr);
	editSolutionButton->setEnabled(currentTask() != nullptr);

	if (! generateOk) {
		setStatus(tr("Generation failed."));
		emit statusChanged(tr("Generation failed: %1").arg(pendingResult.error));
		appendLog(tr("Failed: %1").arg(pendingResult.error));
		QMessageBox::warning(this, tr("Generate Data"), pendingResult.error);
		return;
	}

	setStatus(tr("Done."));
	appendLog(tr("Done: %1 test case(s) and %2 sample(s).")
	              .arg(pendingResult.testCount())
	              .arg(pendingResult.sampleAnswers.size()));
	emit statusChanged(tr("Generated %1 test case(s) and %2 sample(s).")
	                       .arg(pendingResult.testCount())
	                       .arg(pendingResult.sampleAnswers.size()));

	if (! pendingOptions.configureTestCases) {
		appendLog(tr("The test cases were left untouched."));
		return;
	}

	Task *task = currentTask();

	if (! task)
		return;

	int timeLimit = task->getDefaultTimeLimit();
	int memoryLimit = task->getDefaultMemoryLimit();

	if (timeLimit <= 0)
		timeLimit = settings ? settings->getDefaultTimeLimit() : 1000;

	if (memoryLimit <= 0)
		memoryLimit = settings ? settings->getDefaultMemoryLimit() : 128;

	DataGenerator::applyToTask(task, pendingResult, pendingOptions.subtaskMode, timeLimit, memoryLimit);

	const int configured = task->getTestCaseList().size();

	if (pendingOptions.subtaskMode) {
		setStatus(tr("Configured %1 subtask(s).").arg(configured));
		emit statusChanged(tr("Data generated; %1 subtask(s) configured.").arg(configured));
	} else {
		setStatus(tr("Configured %1 test case(s).").arg(configured));
		emit statusChanged(tr("Data generated; %1 test case(s) configured.").arg(configured));
	}

	emit contestModified();
}

void DataWidget::openGeneratorFile() { openSourceFile(QStringLiteral("gen")); }

void DataWidget::openSolutionFile() { openSourceFile(QStringLiteral("sol")); }

void DataWidget::openSourceFile(const QString &baseName) {
	Task *task = currentTask();

	if (! task) {
		QMessageBox::information(this, tr("Generate Data"), tr("There is no task in this contest day yet."));
		return;
	}

	const QString taskDir = Settings::dataPath() + task->getDirectoryName();

	if (! QDir().mkpath(DataGenerator::generatorDirectory(taskDir))) {
		QMessageBox::warning(this, tr("Generate Data"),
		                     tr("Cannot create %1").arg(QDir::toNativeSeparators(
		                         DataGenerator::generatorDirectory(taskDir))));
		return;
	}

	if (baseName == QStringLiteral("gen"))
		DataGenerator::ensureGeneratorTemplate(taskDir);
	else
		DataGenerator::ensureSolutionTemplate(taskDir);

	const QString path = DataGenerator::sourceFile(taskDir, baseName);

	if (path.isEmpty()) {
		QMessageBox::warning(this, tr("Generate Data"),
		                     tr("Cannot create %1").arg(QDir::toNativeSeparators(
		                         DataGenerator::generatorDirectory(taskDir) + QChar('/') + baseName +
		                         QStringLiteral(".cpp"))));
		return;
	}

	if (! QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath())))
		QMessageBox::warning(this, tr("Generate Data"), tr("Cannot open %1").arg(QDir::toNativeSeparators(path)));
}

void DataWidget::openGeneratorFolder() {
	Task *task = currentTask();

	if (! task) {
		QMessageBox::information(this, tr("Generate Data"), tr("There is no task in this contest day yet."));
		return;
	}

	const QString directory = Settings::dataPath() + task->getDirectoryName() + QStringLiteral("/generator");

	if (! QDir().mkpath(directory)) {
		QMessageBox::warning(this, tr("Generate Data"),
		                     tr("Cannot create %1").arg(QDir::toNativeSeparators(directory)));
		return;
	}

	DataGenerator::ensureGeneratorTemplate(Settings::dataPath() + task->getDirectoryName());
	QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(directory).absoluteFilePath()));
}

void DataWidget::appendLog(const QString &line) {
	if (line.isEmpty())
		return;

	if (logView)
		logView->appendPlainText(line);
}

void DataWidget::setStatus(const QString &text) {
	if (statusLabel)
		statusLabel->setText(text);
}
