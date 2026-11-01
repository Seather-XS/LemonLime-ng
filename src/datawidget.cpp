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
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace {
	constexpr int runTimeLimit = 120 * 1000;

	enum RowState { RowPending = 0, RowRunning, RowDone, RowFailed };

	QString stateColor(int state) {
		switch (state) {
			case RowRunning:
				return QStringLiteral("#d39e00");
			case RowDone:
				return QStringLiteral("#2e9e44");
			case RowFailed:
				return QStringLiteral("#d64545");
			default:
				return QStringLiteral("#8c8c8c");
		}
	}

	QString stateText(int state) {
		switch (state) {
			case RowRunning:
				return DataWidget::tr("Generating");
			case RowDone:
				return DataWidget::tr("Finished");
			case RowFailed:
				return DataWidget::tr("Failed");
			default:
				return DataWidget::tr("Not started");
		}
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

	// Selects whole 3-column test point groups instead of single cells or whole rows.
	class GroupTable : public QTableWidget {
	  public:
		explicit GroupTable(QWidget *parent = nullptr) : QTableWidget(parent) {
			setSelectionBehavior(QAbstractItemView::SelectItems);
			setSelectionMode(QAbstractItemView::ExtendedSelection);
		}

		void selectOnlyGroup(int row, int half) { applyGroups({row * 2 + half}); }

		bool isGroupSelected(int row, int half) const {
			const int group = groupAt(row, half);

			if (group < 0)
				return false;

			const QModelIndex index = model()->index(row, half * 3);
			return selectionModel()->isSelected(index);
		}

	  protected:
		void mousePressEvent(QMouseEvent *event) override {
			if (event->button() != Qt::LeftButton) {
				QTableWidget::mousePressEvent(event);
				return;
			}

			const QModelIndex index = indexAt(event->pos());
			const int group = index.isValid() ? groupAt(index.row(), index.column() >= 3 ? 1 : 0) : -1;

			if (group < 0) {
				clearSelection();
				anchor = -1;
				event->accept();
				return;
			}

			if (event->modifiers() & Qt::ControlModifier) {
				QList<int> groups = selectedGroups();

				if (groups.contains(group))
					groups.removeAll(group);
				else
					groups.append(group);

				applyGroups(groups);
				anchor = group;
			} else if (event->modifiers() & Qt::ShiftModifier && anchor >= 0) {
				applyRange(anchor, group);
			} else {
				anchor = group;
				applyGroups({group});
			}

			pressPos = event->pos();
			dragging = true;
			event->accept();
		}

		void mouseMoveEvent(QMouseEvent *event) override {
			if (! dragging || ! (event->buttons() & Qt::LeftButton)) {
				QTableWidget::mouseMoveEvent(event);
				return;
			}

			if (pressPos.x() >= 0)
				applyRubberBand(QRect(pressPos, event->pos()).normalized());

			event->accept();
		}

		void mouseReleaseEvent(QMouseEvent *event) override {
			dragging = false;
			pressPos = QPoint(-1, -1);
			QTableWidget::mouseReleaseEvent(event);
		}

	  private:
		int groupAt(int row, int half) const {
			if (row < 0 || row >= rowCount())
				return -1;

			const QTableWidgetItem *item = this->item(row, half * 3);

			if (! item || ! (item->flags() & Qt::ItemIsSelectable))
				return -1;

			return row * 2 + half;
		}

		QList<int> selectedGroups() const {
			QList<int> groups;

			for (int row = 0; row < rowCount(); row++)
				for (int half = 0; half < 2; half++)
					if (isGroupSelected(row, half))
						groups.append(row * 2 + half);

			return groups;
		}

		void applyRange(int anchorGroup, int targetGroup) {
			const int from = qMin(anchorGroup / 2, targetGroup / 2);
			const int to = qMax(anchorGroup / 2, targetGroup / 2);
			QList<int> groups;

			for (int row = from; row <= to; row++)
				for (int half = 0; half < 2; half++)
					if (groupAt(row, half) >= 0)
						groups.append(row * 2 + half);

			applyGroups(groups);
		}

		QRect groupRect(int row, int half) const {
			return visualRect(model()->index(row, half * 3))
			    .united(visualRect(model()->index(row, half * 3 + 2)));
		}

		void applyRubberBand(const QRect &band) {
			QList<int> groups;

			for (int row = 0; row < rowCount(); row++)
				for (int half = 0; half < 2; half++)
					if (groupAt(row, half) >= 0 && band.intersects(groupRect(row, half)))
						groups.append(row * 2 + half);

			applyGroups(groups);
		}

		void applyGroups(const QList<int> &groups) {
			QItemSelection selection;

			for (int group : groups) {
				const int row = group / 2;
				const int half = group % 2;

				if (groupAt(row, half) < 0)
					continue;

				selection.select(model()->index(row, half * 3), model()->index(row, half * 3 + 2));
			}

			selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
		}

		int anchor{-1};
		bool dragging{false};
		QPoint pressPos{-1, -1};
	};
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
	subtaskLabel = new QLabel(tr("Test Points per Subtask"), settingsBox);
	subtaskEdit = new QLineEdit(settingsBox);
	subtaskEdit->setPlaceholderText(QStringLiteral("5,5,5,10"));
	subtaskEdit->setToolTip(tr("Number of test points in each subtask, separated by commas."));

	auto *casesRow = new QWidget(settingsBox);
	auto *casesLayout = new QHBoxLayout(casesRow);
	casesLayout->setContentsMargins(0, 0, 0, 0);
	casesLayout->addWidget(new QLabel(tr("Test Cases"), casesRow));
	casesLayout->addWidget(caseSpin);
	casesLayout->addSpacing(16);
	casesLayout->addWidget(subtaskLabel);
	casesLayout->addWidget(subtaskEdit, 1);
	form->addRow(QString(), casesRow);

	subtaskNamingBox = new QCheckBox(tr("Name Files by Subtask"), settingsBox);
	subtaskNamingBox->setToolTip(tr("\"<subid>\" becomes usable and \"<testid>\" counts within each subtask. "
	                                "Otherwise the test points are numbered in generation order."));
	form->addRow(QString(), subtaskNamingBox);

	sampleSpin = new QSpinBox(settingsBox);
	sampleSpin->setRange(0, 1000);
	sampleSpin->setValue(0);
	sampleSpin->setToolTip(tr("Samples are generated with sub_id -1 and written to down/."));
	samplePatternEdit = new QLineEdit(settingsBox);
	samplePatternEdit->setPlaceholderText(QStringLiteral("<name><testid>"));
	samplePatternEdit->setToolTip(tr("Leave empty to use the file name prefix. \"<subid>\" is dropped for samples, "
	                                 "and \"<testid>\" is the sample index."));

	auto *sampleRow = new QWidget(settingsBox);
	auto *sampleLayout = new QHBoxLayout(sampleRow);
	sampleLayout->setContentsMargins(0, 0, 0, 0);
	sampleLayout->addWidget(new QLabel(tr("Samples"), sampleRow));
	sampleLayout->addWidget(sampleSpin);
	sampleLayout->addSpacing(16);
	sampleLayout->addWidget(new QLabel(tr("Sample File Name Prefix"), sampleRow));
	sampleLayout->addWidget(samplePatternEdit, 1);
	form->addRow(QString(), sampleRow);

	inputSuffixEdit = new QLineEdit(QStringLiteral(".in"), settingsBox);
	outputSuffixEdit = new QLineEdit(QStringLiteral(".ans"), settingsBox);
	sampleSuffixEdit = new QLineEdit(QStringLiteral(".ans"), settingsBox);

	auto *suffixRow = new QWidget(settingsBox);
	auto *suffixLayout = new QHBoxLayout(suffixRow);
	suffixLayout->setContentsMargins(0, 0, 0, 0);

	for (QLineEdit *edit : {inputSuffixEdit, outputSuffixEdit, sampleSuffixEdit})
		edit->setMaximumWidth(120);

	suffixLayout->addWidget(new QLabel(tr("Input Suffix"), suffixRow));
	suffixLayout->addWidget(inputSuffixEdit, 1);
	suffixLayout->addSpacing(12);
	suffixLayout->addWidget(new QLabel(tr("Output Suffix"), suffixRow));
	suffixLayout->addWidget(outputSuffixEdit, 1);
	suffixLayout->addSpacing(12);
	suffixLayout->addWidget(new QLabel(tr("Sample Output Suffix"), suffixRow));
	suffixLayout->addWidget(sampleSuffixEdit, 1);
	suffixLayout->addStretch(2);
	form->addRow(QString(), suffixRow);

	overwriteBox = new QCheckBox(tr("Overwrite the existing test cases"), settingsBox);
	overwriteBox->setChecked(true);
	overwriteBox->setToolTip(tr("Replace the test cases of this task with the generated ones, and clear the old "
	                            "files in data/ and down/ first."));
	appendBox = new QCheckBox(tr("Append the test cases"), settingsBox);
	appendBox->setToolTip(tr("Keep the existing test cases and the old files; the generation stops when a file "
	                         "name is already taken. Leave both unchecked to keep the test cases untouched."));

	auto *writeRow = new QWidget(settingsBox);
	auto *writeLayout = new QHBoxLayout(writeRow);
	writeLayout->setContentsMargins(0, 0, 0, 0);
	writeLayout->addWidget(overwriteBox);
	writeLayout->addSpacing(16);
	writeLayout->addWidget(appendBox);
	writeLayout->addStretch();
	form->addRow(QString(), writeRow);

	rightLayout->addWidget(settingsBox);

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
	regenerateButton = new QPushButton(tr("Regenerate Selected"), right);
	regenerateButton->setToolTip(tr("Drag over the rows below to select test points, then regenerate them."));
	buttonBar->addWidget(generateButton);
	buttonBar->addWidget(openFolderButton);
	buttonBar->addWidget(editGeneratorButton);
	buttonBar->addWidget(editSolutionButton);
	buttonBar->addWidget(regenerateButton);
	statusLabel = new QLabel(right);
	statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	buttonBar->addWidget(statusLabel, 1);
	rightLayout->addLayout(buttonBar);

	auto *resultBox = new QGroupBox(tr("Log"), right);
	auto *resultLayout = new QVBoxLayout(resultBox);
	resultTable = new GroupTable(resultBox);
	resultTable->setColumnCount(6);
	resultTable->setHorizontalHeaderLabels(columnLabels());
	resultTable->verticalHeader()->setVisible(false);
	resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	resultTable->setTabKeyNavigation(false);
	resultTable->setAlternatingRowColors(true);
	resultTable->setShowGrid(false);
	resultTable->setWordWrap(false);
	resultTable->setContextMenuPolicy(Qt::CustomContextMenu);

	for (int base : {0, 3}) {
		resultTable->horizontalHeader()->setSectionResizeMode(base, QHeaderView::ResizeToContents);
		resultTable->horizontalHeader()->setSectionResizeMode(base + 1, QHeaderView::Stretch);
		resultTable->horizontalHeader()->setSectionResizeMode(base + 2, QHeaderView::Fixed);
		resultTable->setColumnWidth(base + 2, 130);
	}

	resultTable->horizontalHeader()->setHighlightSections(false);
	resultLayout->addWidget(resultTable);
	rightLayout->addWidget(resultBox, 1);

	layout->addWidget(listBox, 1);
	layout->addWidget(right, 3);

	connect(taskList, &QListWidget::currentRowChanged, this, &DataWidget::taskSelectionChanged);
	connect(subtaskBox, &QCheckBox::toggled, this, &DataWidget::updateParameterState);
	connect(subtaskNamingBox, &QCheckBox::toggled, this, &DataWidget::updateParameterState);
	connect(caseSpin, &QSpinBox::valueChanged, this, &DataWidget::updateParameterState);
	connect(sampleSpin, &QSpinBox::valueChanged, this, &DataWidget::updateParameterState);
	connect(subtaskEdit, &QLineEdit::textChanged, this, &DataWidget::updateParameterState);
	connect(patternEdit, &QLineEdit::textChanged, this, &DataWidget::updateParameterState);
	connect(samplePatternEdit, &QLineEdit::textChanged, this, &DataWidget::updateParameterState);
	connect(inputSuffixEdit, &QLineEdit::textChanged, this, &DataWidget::updateParameterState);
	connect(overwriteBox, &QCheckBox::toggled, this, [this](bool checked) {
		if (checked && appendBox->isChecked()) {
			const QSignalBlocker blocker(appendBox);
			appendBox->setChecked(false);
		}

		updateParameterState();
	});
	connect(appendBox, &QCheckBox::toggled, this, [this](bool checked) {
		if (checked && overwriteBox->isChecked()) {
			const QSignalBlocker blocker(overwriteBox);
			overwriteBox->setChecked(false);
		}

		updateParameterState();
	});
	connect(generateButton, &QPushButton::clicked, this, &DataWidget::generateClicked);
	connect(openFolderButton, &QPushButton::clicked, this, &DataWidget::openGeneratorFolder);
	connect(editGeneratorButton, &QPushButton::clicked, this, &DataWidget::openGeneratorFile);
	connect(editSolutionButton, &QPushButton::clicked, this, &DataWidget::openSolutionFile);
	connect(regenerateButton, &QPushButton::clicked, this, &DataWidget::regenerateSelected);
	connect(resultTable, &QTableWidget::itemSelectionChanged, this, &DataWidget::updateRegenerateState);
	connect(resultTable, &QTableWidget::customContextMenuRequested, this, &DataWidget::showResultMenu);
	connect(generator, &DataGenerator::fileStarted, this, &DataWidget::fileStarted);
	connect(generator, &DataGenerator::fileFinished, this, &DataWidget::fileFinished);

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
	regenerateButton->setText(tr("Regenerate Selected"));

	if (resultTable)
		resultTable->setHorizontalHeaderLabels(columnLabels());

	updateParameterState();
	rebuildResultTable(true);
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

	return taskForDirectory(taskList->currentItem()->data(Qt::UserRole).toString());
}

Task *DataWidget::taskForDirectory(const QString &directory) const {
	if (! curContest)
		return nullptr;

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

	rebuildResultTable();
	scheduleSaveConfig();
}

void DataWidget::rebuildResultTable(bool force) {
	if (! resultTable || generateThread)
		return;

	Task *task = currentTask();
	DataGenerator::Options options;
	QList<DataGenerator::PlannedTest> tests;

	if (task) {
		options = uiOptions();
		QString error;
		tests = DataGenerator::planTests(options, task->getDirectoryName(), &error);
	}

	QString signature = task ? task->getDirectoryName() : QString();
	signature += QChar(0x1f) + options.namePattern + QChar(0x1f) + options.sampleNamePattern +
	             QChar(0x1f) + options.inputSuffix + QChar(0x1f) + options.outputSuffix + QChar(0x1f) +
	             options.sampleSuffix + QChar(0x1f) + (options.subtaskMode ? QChar('1') : QChar('0')) +
	             (options.subtaskNaming ? QChar('1') : QChar('0'));

	for (const DataGenerator::PlannedTest &test : std::as_const(tests))
		signature += QChar('\n') + test.fileName + (test.sample ? QChar('s') : QChar('t')) +
		             QString::number(test.testId);

	if (! force && signature == planSignature)
		return;

	planSignature = signature;
	plan = tests;

	for (QLabel *label : std::as_const(planStatusLabels)) {
		if (! label)
			continue;

		label->hide();
		label->deleteLater();
	}

	resultTable->clearContents();
	resultTable->setRowCount(0);
	resultTable->clearSpans();
	rowStates.clear();
	rowDetails.clear();
	rowPaths.clear();
	planStatusLabels.clear();
	rowPlans.clear();
	planToRow.clear();
	currentDetailRow = -1;

	if (tests.isEmpty()) {
		updateRegenerateState();
		return;
	}

	int testCount = 0;
	int sampleCount = 0;

	for (const DataGenerator::PlannedTest &test : std::as_const(tests))
		(test.sample ? sampleCount : testCount)++;

	const int testRows = (testCount + 1) / 2;
	const bool divider = testCount > 0 && sampleCount > 0;
	const int sampleRowStart = testRows + (divider ? 1 : 0);
	const int sampleRows = (sampleCount + 1) / 2;
	const int rowCount = sampleRowStart + sampleRows;

	// Test points fill the left half top-to-bottom first, then the right half.
	const int testLeftCount = (testCount + 1) / 2;
	const int sampleLeftCount = (sampleCount + 1) / 2;

	resultTable->setRowCount(rowCount);
	planToRow.fill(-1, tests.size());
	rowPlans.clear();

	for (int row = 0; row < rowCount; row++)
		rowPlans.append(QList<int>());

	planStatusLabels.fill(nullptr, tests.size());

	const int lineHeight = resultTable->fontMetrics().height();

	if (divider) {
		auto *dividerItem = new QTableWidgetItem(tr("Sample Data"));
		dividerItem->setFlags(Qt::ItemIsEnabled);
		dividerItem->setTextAlignment(Qt::AlignCenter);
		QFont font = dividerItem->font();
		font.setBold(true);
		dividerItem->setFont(font);
		dividerItem->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
		resultTable->setItem(testRows, 0, dividerItem);
		resultTable->setSpan(testRows, 0, 1, 6);
		resultTable->setRowHeight(testRows, lineHeight + 10);
	}

	QHash<int, int> subtaskCounters;
	int testPosition = 0;
	int samplePosition = 0;

	for (int index = 0; index < tests.size(); index++) {
		const DataGenerator::PlannedTest &test = tests.at(index);
		const int position = test.sample ? samplePosition++ : testPosition++;
		const int leftCount = test.sample ? sampleLeftCount : testLeftCount;
		const bool rightHalf = position >= leftCount;
		const int row = (test.sample ? sampleRowStart : 0) +
		                (rightHalf ? position - leftCount : position);
		const int column = rightHalf ? 3 : 0;

		QString idText;

		if (test.sample) {
			idText = QString::number(test.testId);
		} else if (options.subtaskMode) {
			idText = QStringLiteral("%1-%2").arg(test.subtask).arg(++subtaskCounters[test.subtask]);
		} else {
			idText = QString::number(test.testId);
		}

		auto *idItem = new QTableWidgetItem(idText);
		idItem->setTextAlignment(Qt::AlignCenter);
		resultTable->setItem(row, column, idItem);

		const QString inputName = test.fileName + options.inputSuffix;
		const QString outputName =
		    test.fileName + (test.sample ? options.sampleSuffix : options.outputSuffix);
		const QString folder = test.sample ? QStringLiteral("down") : QStringLiteral("data");
		const QString directory = Settings::dataPath() + task->getDirectoryName() + QChar('/');

		auto *fileLabel = new QLabel(resultTable);
		fileLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		fileLabel->setContentsMargins(4, 0, 4, 0);
		fileLabel->setText(inputName + QChar('\n') + outputName);
		fileLabel->setToolTip(QDir::toNativeSeparators(directory + folder + QChar('/') + inputName) +
		                      QChar('\n') + QDir::toNativeSeparators(directory + folder + QChar('/') +
		                                                             outputName));
		resultTable->setCellWidget(row, column + 1, fileLabel);

		auto *statusLabel = new QLabel(resultTable);
		statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		statusLabel->setContentsMargins(4, 0, 4, 0);
		statusLabel->setTextFormat(Qt::RichText);
		resultTable->setCellWidget(row, column + 2, statusLabel);

		rowStates.append(RowPending);
		rowStates.append(RowPending);
		rowDetails.append(QStringList());
		rowDetails.append(QStringList());
		rowPaths.append(QStringLiteral("%1/%2").arg(folder, inputName));
		rowPaths.append(QStringLiteral("%1/%2").arg(folder, outputName));
		planStatusLabels[index] = statusLabel;
		rowPlans[row].append(index);
		planToRow[index] = row;
		resultTable->setRowHeight(row, lineHeight * 2 + 12);
		refreshRowLabel(index);
	}

	applyStoredStatuses();
	updateRegenerateState();
}

void DataWidget::refreshRowLabel(int planIndex) {
	QLabel *label = planStatusLabels.value(planIndex);

	if (! label)
		return;

	QStringList lines;
	QStringList tips;

	for (int stage = 0; stage < 2; stage++) {
		const int file = planIndex * 2 + stage;
		const int state = rowStates.value(file);

		QString text = QStringLiteral("<span style=\"color:%1\">%2</span>")
		                   .arg(stateColor(state), stateText(state));

		lines.append(text);

		QString tip = stateText(state);

		for (const QString &detail : rowDetails.value(file))
			tip += QChar('\n') + detail;

		tips.append(tip);
	}

	label->setText(lines.join(QStringLiteral("<br/>")));
	label->setToolTip(tips.join(QChar('\n')));
}

void DataWidget::setRowStatus(int fileRow, int status) {
	if (fileRow < 0 || fileRow >= rowStates.size())
		return;

	rowStates[fileRow] = status;
	refreshRowLabel(fileRow / 2);
}

QStringList DataWidget::columnLabels() const {
	const QStringList labels{tr("Test Point"), tr("File"), tr("Status")};

	return labels + labels;
}

void DataWidget::showResultMenu(const QPoint &pos) {
	auto *table = static_cast<GroupTable *>(resultTable);
	const QModelIndex index = table->indexAt(pos);
	const int planIndex = planIndexAt(index);

	if (planIndex >= 0 && ! table->isGroupSelected(index.row(), index.column() >= 3 ? 1 : 0))
		table->selectOnlyGroup(index.row(), index.column() >= 3 ? 1 : 0);

	QMenu menu(this);
	QAction *regenerate = menu.addAction(tr("Regenerate the selected data"));
	regenerate->setEnabled(generateThread == nullptr && ! selectedPlanIndices().isEmpty());
	connect(regenerate, &QAction::triggered, this, &DataWidget::regenerateSelected);
	menu.exec(table->viewport()->mapToGlobal(pos));
}

int DataWidget::planIndexAt(const QModelIndex &index) const {
	if (! index.isValid())
		return -1;

	const int half = index.column() >= 3 ? 1 : 0;
	const QList<int> indices = rowPlans.value(index.row());

	return indices.value(half, -1);
}

QList<int> DataWidget::selectedPlanIndices() const {
	QList<int> indices;

	if (! resultTable || ! resultTable->selectionModel())
		return indices;

	for (const QModelIndex &index : resultTable->selectionModel()->selectedIndexes()) {
		const int half = index.column() >= 3 ? 1 : 0;
		const int planIndex = rowPlans.value(index.row()).value(half, -1);

		if (planIndex >= 0 && ! indices.contains(planIndex))
			indices.append(planIndex);
	}

	std::sort(indices.begin(), indices.end());
	return indices;
}

void DataWidget::updateRegenerateState() {
	if (! regenerateButton)
		return;

	regenerateButton->setEnabled(generateThread == nullptr && ! selectedPlanIndices().isEmpty());
}

void DataWidget::applyStoredStatuses() {
	Task *task = currentTask();

	if (! task)
		return;

	const QList<DataGenerator::FileStatus> stored =
	    DataGenerator::loadStatuses(Settings::dataPath() + task->getDirectoryName());

	if (stored.isEmpty())
		return;

	QHash<QString, DataGenerator::FileStatus> byPath;

	for (const DataGenerator::FileStatus &status : stored)
		byPath.insert(status.path, status);

	for (int row = 0; row < rowPaths.size(); row++) {
		const auto it = byPath.constFind(rowPaths.at(row));

		if (it == byPath.constEnd())
			continue;

		rowStates[row] = it->state == RowRunning ? RowPending : it->state;
		rowDetails[row] = it->details;
	}

	for (int planIndex = 0; planIndex < plan.size(); planIndex++)
		refreshRowLabel(planIndex);
}

void DataWidget::saveResultStatuses() {
	if (pendingTaskDirectory.isEmpty() || rowPaths.isEmpty())
		return;

	QList<DataGenerator::FileStatus> statuses;

	for (int row = 0; row < rowPaths.size(); row++) {
		DataGenerator::FileStatus status;
		status.path = rowPaths.at(row);
		status.state = rowStates.value(row) == RowRunning ? RowPending : rowStates.value(row);
		status.details = rowDetails.value(row);
		statuses.append(status);
	}

	DataGenerator::saveStatuses(Settings::dataPath() + pendingTaskDirectory, statuses);
}

void DataWidget::resetRowStates() {
	for (int row = 0; row < rowStates.size(); row++) {
		rowDetails[row].clear();
		rowStates[row] = RowPending;
	}

	for (int planIndex = 0; planIndex < plan.size(); planIndex++)
		refreshRowLabel(planIndex);
}

void DataWidget::resetPlanStates(const QList<int> &indices) {
	for (int planIndex : indices) {
		for (int stage = 0; stage < 2; stage++) {
			const int file = planIndex * 2 + stage;

			if (file < 0 || file >= rowStates.size())
				continue;

			rowStates[file] = RowPending;
			rowDetails[file].clear();
		}

		refreshRowLabel(planIndex);
	}
}

void DataWidget::regenerateSelected() { regenerateTests(selectedPlanIndices()); }

void DataWidget::regenerateTests(const QList<int> &indices) {
	if (generateThread || indices.isEmpty())
		return;

	Task *task = currentTask();

	if (! task)
		return;

	DataGenerator::Options options;
	QString error;

	if (! collectOptions(task, &options, &error)) {
		QMessageBox::warning(this, tr("Generate Data"), error);
		return;
	}

	QList<int> chosen;

	for (int index : indices)
		if (index >= 0 && index < plan.size() && ! chosen.contains(index))
			chosen.append(index);

	if (chosen.isEmpty())
		return;

	std::sort(chosen.begin(), chosen.end());
	resetPlanStates(chosen);
	options.onlyTests = chosen;
	pendingOptions = options;
	pendingPartial = true;
	pendingIndices = chosen;
	pendingTaskDirectory = task->getDirectoryName();
	pendingResult = DataGenerator::Result();
	generateOk = false;
	currentDetailRow = -1;
	generateButton->setEnabled(false);
	openFolderButton->setEnabled(false);
	editGeneratorButton->setEnabled(false);
	editSolutionButton->setEnabled(false);
	regenerateButton->setEnabled(false);

	QString message;

	if (chosen.size() == 1)
		message = tr("Regenerating %1 ...").arg(plan.at(chosen.first()).fileName);
	else
		message = tr("Regenerating %1 test point(s) ...").arg(chosen.size());

	setStatus(message);
	emit statusChanged(message);

	generateThread = QThread::create([this] { generateOk = generator->run(pendingOptions, &pendingResult); });
	connect(generateThread, &QThread::finished, this, &DataWidget::generateFinished);
	generateThread->start();
}

void DataWidget::fileStarted(int testIndex, int stage) {
	const int row = testIndex * 2 + stage;

	if (row < 0 || row >= rowStates.size())
		return;

	currentDetailRow = row;
	rowDetails[row].clear();
	setRowStatus(row, RowRunning);
}

void DataWidget::fileFinished(int testIndex, int stage, bool ok, const QString &detail) {
	const int row = testIndex * 2 + stage;

	if (row < 0 || row >= rowStates.size())
		return;

	if (! detail.isEmpty())
		rowDetails[row].append(detail);

	currentDetailRow = -1;
	setRowStatus(row, ok ? RowDone : RowFailed);
}

DataGenerator::Options DataWidget::uiOptions() const {
	DataGenerator::Options options;

	if (! patternEdit)
		return options;

	options.namePattern = patternEdit->text().trimmed();
	options.sampleNamePattern = samplePatternEdit->text().trimmed();
	options.inputSuffix = inputSuffixEdit->text().trimmed();
	options.outputSuffix = outputSuffixEdit->text().trimmed();
	options.sampleSuffix = sampleSuffixEdit->text().trimmed();
	options.subtaskMode = subtaskBox->isChecked();
	options.caseCount = caseSpin->value();
	options.sampleCount = sampleSpin->value();
	options.subtaskNaming = subtaskNamingBox->isChecked();

	if (appendBox->isChecked())
		options.writeMode = DataGenerator::Options::WriteMode::Append;
	else if (overwriteBox->isChecked())
		options.writeMode = DataGenerator::Options::WriteMode::Overwrite;
	else
		options.writeMode = DataGenerator::Options::WriteMode::None;

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
	const QSignalBlocker samplePatternBlocker(samplePatternEdit);
	const QSignalBlocker subtaskBlocker(subtaskBox);
	const QSignalBlocker caseBlocker(caseSpin);
	const QSignalBlocker namingBlocker(subtaskNamingBox);
	const QSignalBlocker sampleBlocker(sampleSpin);
	const QSignalBlocker inputBlocker(inputSuffixEdit);
	const QSignalBlocker outputBlocker(outputSuffixEdit);
	const QSignalBlocker suffixBlocker(sampleSuffixEdit);
	const QSignalBlocker overwriteBlocker(overwriteBox);
	const QSignalBlocker appendBlocker(appendBox);

	patternEdit->setText(options.namePattern);
	samplePatternEdit->setText(options.sampleNamePattern);
	inputSuffixEdit->setText(options.inputSuffix);
	outputSuffixEdit->setText(options.outputSuffix);
	sampleSuffixEdit->setText(options.sampleSuffix);
	subtaskBox->setChecked(options.subtaskMode);
	subtaskNamingBox->setChecked(options.subtaskNaming);
	caseSpin->setValue(options.caseCount);
	sampleSpin->setValue(options.sampleCount);
	overwriteBox->setChecked(options.writeMode == DataGenerator::Options::WriteMode::Overwrite);
	appendBox->setChecked(options.writeMode == DataGenerator::Options::WriteMode::Append);

	QStringList counts;

	for (int count : options.subtaskCases)
		counts.append(QString::number(count));

	subtaskEdit->setText(counts.join(QChar(',')));
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
				*error = tr("The test points per subtask must be positive integers, e.g. \"5,5,4,6\".");
				return false;
			}
		}

		if (options->subtaskCases.isEmpty()) {
			*error = tr("Fill in the test points per subtask, e.g. \"5,5,4,6\".");
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
		QMessageBox::warning(this, tr("Generate Data"), error);
		return;
	}

	generateOk = false;
	pendingResult = DataGenerator::Result();
	rebuildResultTable(true);
	resetRowStates();
	pendingPartial = false;
	pendingIndices.clear();
	pendingTaskDirectory = task->getDirectoryName();
	currentDetailRow = -1;
	generateButton->setEnabled(false);
	openFolderButton->setEnabled(false);
	editGeneratorButton->setEnabled(false);
	editSolutionButton->setEnabled(false);
	regenerateButton->setEnabled(false);
	setStatus(tr("Generating ..."));
	emit statusChanged(tr("Generating ..."));

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
	updateRegenerateState();
	currentDetailRow = -1;

	if (! generateOk) {
		saveResultStatuses();
		setStatus(tr("Generation failed."));
		emit statusChanged(tr("Generation failed: %1").arg(pendingResult.error));
		QMessageBox::warning(this, tr("Generate Data"), pendingResult.error);
		return;
	}

	if (pendingPartial) {
		QString message;

		if (pendingIndices.size() == 1 && pendingIndices.first() < plan.size())
			message = tr("Regenerated %1.").arg(plan.at(pendingIndices.first()).fileName);
		else
			message = tr("Regenerated %1 test point(s).").arg(pendingIndices.size());

		saveResultStatuses();
		setStatus(message);
		emit statusChanged(message);
		return;
	}

	setStatus(tr("Done."));
	emit statusChanged(tr("Generated %1 test case(s) and %2 sample(s).")
	                       .arg(pendingResult.testCount())
	                       .arg(pendingResult.sampleAnswers.size()));

	if (pendingOptions.writeMode == DataGenerator::Options::WriteMode::None) {
		saveResultStatuses();
		setStatus(tr("The test cases were left untouched."));
		return;
	}

	Task *task = taskForDirectory(pendingTaskDirectory);

	if (! task)
		return;

	int timeLimit = task->getDefaultTimeLimit();
	int memoryLimit = task->getDefaultMemoryLimit();

	if (timeLimit <= 0)
		timeLimit = settings ? settings->getDefaultTimeLimit() : 1000;

	if (memoryLimit <= 0)
		memoryLimit = settings ? settings->getDefaultMemoryLimit() : 128;

	const bool append = pendingOptions.writeMode == DataGenerator::Options::WriteMode::Append;
	const int before = task->getTestCaseList().size();

	DataGenerator::applyToTask(task, pendingResult, pendingOptions.subtaskMode, timeLimit, memoryLimit,
	                           append);

	const int configured = task->getTestCaseList().size();

	if (pendingOptions.subtaskMode) {
		setStatus(tr("Configured %1 subtask(s).").arg(configured));
		emit statusChanged(append ? tr("Data generated; %1 subtask(s) appended (%2 in total).")
		                                .arg(configured - before)
		                                .arg(configured)
		                          : tr("Data generated; %1 subtask(s) configured.").arg(configured));
	} else {
		setStatus(tr("Configured %1 test case(s).").arg(configured));
		emit statusChanged(append ? tr("Data generated; %1 test case(s) appended (%2 in total).")
		                                .arg(configured - before)
		                                .arg(configured)
		                          : tr("Data generated; %1 test case(s) configured.").arg(configured));
	}

	saveResultStatuses();
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

	if (currentDetailRow < 0 || currentDetailRow >= rowDetails.size())
		return;

	rowDetails[currentDetailRow].append(line);
	refreshRowLabel(currentDetailRow / 2);
}

void DataWidget::setStatus(const QString &text) {
	if (statusLabel)
		statusLabel->setText(text);
}
