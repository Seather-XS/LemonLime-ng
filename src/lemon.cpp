/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lemon.h"
#include "ui_lemon.h"
#include "addcompilerwizard.h"
#include "addtaskdialog.h"
#include "base/LemonBase.hpp"
#include "base/LemonLog.hpp"
#include "base/LemonTranslator.hpp"
#include "base/compiler.h"
#include "base/settings.h"
#include "component/exportutil/exportutil.h"
#include "contestsettingsdialog.h"
#include "core/contest.h"
#include "core/contestant.h"
#include "core/task.h"
#include "core/testcase.h"
#include "datawidget.h"
#include "daydialog.h"
#include "detaildialog.h"
#include "exportwidget.h"
#include "newcontestdialog.h"
#include "opencontestdialog.h"
#include "optionsdialog.h"
#include "statisticsbrowser.h"
#include "statementeditwidget.h"
#include "welcomedialog.h"
#include <QByteArrayView>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSaveFile>
#include <QStatusBar>
#include <QTextBrowser>
#include <QUrl>
#include <algorithm>
#include <chrono>
#define LEMON_MODULE_NAME "Lemon"

LemonLime::LemonLime(QWidget *parent) : QMainWindow(parent), ui(new Ui::LemonLime) {
	ui->setupUi(this);
	curContest = nullptr;
	settings = new Settings();
	ui->tabWidget->setVisible(false);
	ui->closeAction->setEnabled(false);
	ui->saveAction->setEnabled(false);
	ui->openFolderAction->setEnabled(false);
	ui->actionChangeContestName->setEnabled(false);
	ui->actionContestSettings->setEnabled(false);
	dataDirWatcher = nullptr;
	dataWatcherTimer = new QTimer(this);
	dataWatcherTimer->setSingleShot(true);
	dataWatcherTimer->setInterval(400);
	connect(dataWatcherTimer, &QTimer::timeout, this, &LemonLime::rebuildDataWatcher);
	settings->loadSettings();
	TaskMenu = new QMenu();
	signalMapper = new QSignalMapper();
	ui->summary->setSettings(settings);
	ui->taskEdit->setSettings(settings);
	ui->testCaseEdit->setSettings(settings);
	connect(this, &LemonLime::dataPathChanged, ui->taskEdit, &TaskEditWidget::dataPathChanged);
	connect(this, &LemonLime::dataPathChanged, ui->testCaseEdit, &TestCaseEditWidget::dataPathChanged);
	connect(ui->summary, &SummaryTree::currentItemChanged, this, &LemonLime::summarySelectionChanged);
	connect(ui->summary, &SummaryTree::taskAboutToBeDeleted, this, &LemonLime::releaseDataWatcher);
	connect(ui->summary, &SummaryTree::taskChanged, this, &LemonLime::resetDataWatcher);
	connect(ui->summary, &SummaryTree::taskChanged, this, [this] {
		if (ui->dataWidget)
			ui->dataWidget->refresh();
	});
	connect(ui->optionsAction, &QAction::triggered, this, &LemonLime::showOptionsDialog);
	connect(ui->actionContestSettings, &QAction::triggered, this, &LemonLime::showContestSettingsDialog);
	connect(ui->cleanupButton, &QPushButton::clicked, this, &LemonLime::cleanupButtonClicked);
	connect(ui->refreshButton, &QPushButton::clicked, this, &LemonLime::refreshButtonClicked);
	connect(ui->judgeButton, &QPushButton::clicked, ui->resultViewer, &ResultViewer::judgeSelected);
	connect(ui->judgeAllButton, &QPushButton::clicked, ui->resultViewer, &ResultViewer::judgeAll);
	connect(ui->judgeUnjudgedButton, &QPushButton::clicked, ui->resultViewer, &ResultViewer::judgeUnjudged);
	connect(ui->judgeAction, &QAction::triggered, ui->resultViewer, &ResultViewer::judgeSelected);
	connect(ui->judgeAllAction, &QAction::triggered, ui->resultViewer, &ResultViewer::judgeAll);
	connect(ui->judgeUnjudgedAction, &QAction::triggered, ui->resultViewer, &ResultViewer::judgeUnjudged);
	connect(ui->cleanupAction, &QAction::triggered, this, &LemonLime::cleanupButtonClicked);
	connect(ui->refreshAction, &QAction::triggered, this, &LemonLime::refreshButtonClicked);
	connect(ui->judgeGreyAction, &QAction::triggered, ui->resultViewer, &ResultViewer::judgeGrey);
	connect(ui->judgeMagentaAction, &QAction::triggered, ui->resultViewer, &ResultViewer::judgeMagenta);
	connect(ui->tabWidget, &QTabWidget::currentChanged, this, &LemonLime::tabIndexChanged);
	connect(ui->moveUpButton, &QToolButton::clicked, this, &LemonLime::moveUpTask);
	connect(ui->moveDownButton, &QToolButton::clicked, this, &LemonLime::moveDownTask);
	connect(ui->resultViewer, &ResultViewer::itemSelectionChanged, this, &LemonLime::viewerSelectionChanged);
	connect(ui->resultViewer, &ResultViewer::contestantDeleted, this, &LemonLime::contestantDeleted);
	connect(ui->statementEdit, &StatementEditWidget::pdfFileNameChanged, ui->exportWidget,
	        &ExportWidget::setDefaultStatementFile);
	connect(ui->dataWidget, &DataWidget::contestModified, this, [this] {
		if (! curContest)
			return;

		ui->summary->setContest(curContest);
		saveContest(curFile);
	});
	connect(ui->dataWidget, &DataWidget::statusChanged, this,
	        [this](const QString &message) { ui->statusBar->showMessage(message); });
	connect(ui->newAction, &QAction::triggered, this, &LemonLime::newAction);
	connect(ui->openAction, &QAction::triggered, this, &LemonLime::loadAction);
	connect(ui->saveAction, &QAction::triggered, this, &LemonLime::saveAction);
	connect(ui->openFolderAction, &QAction::triggered, this, &LemonLime::openFolderAction);
	connect(ui->closeAction, &QAction::triggered, this, &LemonLime::closeAction);
	connect(ui->addTasksAction, &QAction::triggered, this, &LemonLime::addTasksAction);
	connect(ui->addProblemTasksAction, &QAction::triggered, this, &LemonLime::addProblemTasksAction);
	connect(ui->exportAction, &QAction::triggered, this, &LemonLime::exportResult);
	connect(ui->actionExportStatistics, &QAction::triggered, this, &LemonLime::exportStatistics);
	connect(ui->aboutAction, &QAction::triggered, this, &LemonLime::aboutLemon);
	connect(ui->actionManual, &QAction::triggered, this, &LemonLime::actionManual);
	connect(ui->actionMore, &QAction::triggered, this, &LemonLime::actionMore);
	connect(ui->actionChangeContestName, &QAction::triggered, this, &LemonLime::changeContestName);
	connect(ui->actionNewDay, &QAction::triggered, this, &LemonLime::newDayAction);
	connect(ui->actionRemoveDay, &QAction::triggered, this, &LemonLime::removeDayAction);
	connect(ui->actionRenameProject, &QAction::triggered, this, &LemonLime::renameProjectAction);
	ui->actionNewDay->setEnabled(false);
	ui->actionRemoveDay->setEnabled(false);
	ui->actionRenameProject->setEnabled(false);
	ui->menuDays->setEnabled(false);
	connect(ui->exitAction, &QAction::triggered, this, &LemonLime::close);

	QSettings settings("LemonLime", "lemon");
	QSize _size = settings.value("WindowSize", size()).toSize();
	resize(_size);

	autoSaveTimer.callOnTimeout([this]() {
		if (curContest)
			saveAction();
	});
	using namespace std::chrono_literals;
	autoSaveTimer.start(30s);
}

LemonLime::~LemonLime() {
	delete TaskMenu;
	delete ui;
}

void LemonLime::changeEvent(QEvent *event) {
	if (event->type() == QEvent::LanguageChange) {
		ui->retranslateUi(this);
		ui->resultViewer->refreshViewer();
		ui->statisticsBrowser->refresh();
	}
}

void LemonLime::closeEvent(QCloseEvent *) {
	ui->statementEdit->saveIfNeeded();

	if (curContest)
		saveContest(curFile);

	settings->saveSettings();
	QSettings settings("LemonLime", "lemon");
	settings.setValue("WindowSize", size());
}

auto LemonLime::getSplashTime() -> int { return settings->getSplashTime(); }

void LemonLime::welcome() {
	if (settings->getCompilerList().empty()) {
		auto *wizard = new AddCompilerWizard(this);

		if (wizard->exec() == QDialog::Accepted) {
			QList<Compiler *> compilerList = wizard->getCompilerList();

			for (auto &i : compilerList)
				settings->addCompiler(i);
		}

		delete wizard;
	}

	auto *dialog = new WelcomeDialog(this);
	dialog->setRecentContest(settings->getRecentContest());

	if (dialog->exec() == QDialog::Accepted) {
		settings->setRecentContest(dialog->getRecentContest());

		if (dialog->getCurrentTab() == 0) {
			loadContest(dialog->getSelectedContest());
		} else {
			newContest(dialog->getContestTitle(), dialog->getSavingName(), dialog->getContestPath());
		}
	} else {
		settings->setRecentContest(dialog->getRecentContest());
	}

	delete dialog;
}

void LemonLime::insertWatchPath(const QString &curDir, QFileSystemWatcher *watcher) {
	watcher->addPath(curDir);
	QDir dir(curDir);
	QStringList list = dir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);

	for (int i = 0; i < list.size(); i++) {
		insertWatchPath(curDir + list[i] + QDir::separator(), watcher);
	}
}

void LemonLime::resetDataWatcher() {
	if (dataWatcherTimer)
		dataWatcherTimer->start();
}

void LemonLime::releaseDataWatcher() {
	delete dataDirWatcher;
	dataDirWatcher = nullptr;
}

void LemonLime::rebuildDataWatcher() {
	releaseDataWatcher();
	dataDirWatcher = new QFileSystemWatcher(this);
	insertWatchPath(Settings::dataPath(), dataDirWatcher);
	connect(dataDirWatcher, &QFileSystemWatcher::directoryChanged, this, &LemonLime::resetDataWatcher);
	connect(dataDirWatcher, &QFileSystemWatcher::fileChanged, this, &LemonLime::resetDataWatcher);
	emit dataPathChanged();
}

void LemonLime::refreshSummary() {
	if (! ui->summary->isEnabled())
		return;

	ui->summary->setContest(curContest);
}

void LemonLime::summarySelectionChanged() {
	if (! ui->summary->isEnabled())
		return;

	QTreeWidgetItem *curItem = ui->summary->currentItem();

	if (! curItem) {
		ui->taskEdit->setEditTask(nullptr);
		ui->editWidget->setCurrentIndex(0);
		return;
	}

	int index = ui->summary->indexOfTopLevelItem(curItem);

	if (index != -1) {
		ui->taskEdit->setEditTask(curContest->getTask(index));
		ui->editWidget->setCurrentIndex(1);
	} else {
		QTreeWidgetItem *parentItem = curItem->parent();
		int taskIndex = ui->summary->indexOfTopLevelItem(parentItem);
		int testCaseIndex = parentItem->indexOfChild(curItem);
		Task *curTask = curContest->getTask(taskIndex);
		TestCase *curTestCase = curTask->getTestCase(testCaseIndex);
		ui->testCaseEdit->setEditTestCase(curTestCase, curTask->getTaskType() == Task::Traditional ||
		                                                   curTask->getTaskType() == Task::Interaction ||
		                                                   curTask->getTaskType() == Task::Communication ||
		                                                   curTask->getTaskType() == Task::CommunicationExec);
		ui->editWidget->setCurrentIndex(2);
	}
}

void LemonLime::showOptionsDialog() {
	auto *dialog = new OptionsDialog(this);
	dialog->resetEditSettings(settings);

	if (dialog->exec() == QDialog::Accepted) {
		settings->copyFrom(dialog->getEditSettings());
		LemonLimeTranslator->InstallTranslation(settings->getUiLanguage());
		ui->testCaseEdit->setSettings(settings);

		if (curContest) {
			const QList<Task *> &taskList = curContest->getTaskList();

			for (auto *i : taskList)
				i->refreshCompilerConfiguration(settings);
		}
	}

	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	delete dialog;
}

void LemonLime::showContestSettingsDialog() {
	if (! curContest)
		return;

	auto *dialog = new ContestSettingsDialog(this);
	dialog->setSettings(settings);
	dialog->resetEditContest(curContest);

	if (dialog->exec() == QDialog::Accepted) {
		dialog->applyTo(curContest);
		refreshSummary();
		ui->resultViewer->refreshViewer();
		ui->statisticsBrowser->refresh();
		saveContest(curFile);
	}

	delete dialog;
}

void LemonLime::judgeExtButtonFlip(bool stat) {
	ui->judgeAllButton->setEnabled(stat);
	ui->judgeAllAction->setEnabled(stat);
	ui->judgeUnjudgedButton->setEnabled(stat);
	ui->judgeUnjudgedAction->setEnabled(stat);
	ui->judgeGreyAction->setEnabled(stat);
	ui->judgeMagentaAction->setEnabled(stat);
}

void LemonLime::refreshButtonClicked() {
	curContest->refreshContestantList();
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	judgeExtButtonFlip(curContest && ! curContest->getContestantList().isEmpty());
	ui->cleanupAction->setEnabled(true);
	ui->refreshAction->setEnabled(true);
}

void removePath(const QString &path) {
	if (path.isEmpty())
		return;

	QDir dir(path);

	if (! dir.exists())
		return;

	dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

	for (const auto &fi : dir.entryInfoList()) {
		if (fi.isFile() || fi.isSymLink())
			fi.dir().remove(fi.fileName());
		else
			removePath(fi.absoluteFilePath());
	}

	dir.rmpath(dir.absolutePath());
}

void copyPath(const QString &fromPath, const QString &toPath) {
	QDir dir(fromPath);

	if (! dir.exists())
		return;

	QString fpath = fromPath + QDir::separator();
	QString tpath = toPath + QDir::separator();
	dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

	for (const auto &fi : dir.entryInfoList()) {
		QString fn = fpath + fi.fileName();
		QString tn = tpath + fi.fileName();

		if (fi.isFile() || fi.isSymLink())
			QFile::copy(fn, tn);
		else {
			QDir toDir(toPath);
			toDir.mkpath(fi.fileName());
			copyPath(fn, tn);
		}
	}
}

void LemonLime::cleanupButtonClicked() {
	QString text;
	text += tr("Are you sure to Clean up Files?") + "<br>";
	text += tr("Reading guide are recommended.") + "<br>";
	QMessageBox::StandardButton res =
	    QMessageBox::warning(this, tr("Clean up Files"), text,
	                         QMessageBox::Yes | QMessageBox::No | QMessageBox::Abort, QMessageBox::No);

	if (res == QMessageBox::Yes) {
		QDir basDir(Settings::sourcePath());
		basDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
		QFileInfoList basDirLis = basDir.entryInfoList();
		int tarcnt = basDirLis.size();
		QString backupFolder = "source_bak_%1";
		int backupNum = 0;
		QDir tempBackupLoca;

		while (tempBackupLoca.exists(backupFolder.arg(backupNum)))
			backupNum++;

		backupFolder = backupFolder.arg(backupNum);
		text = tr("Making backup files to dir <br> `%1'?").arg(backupFolder) + "<br>";
		QMessageBox::StandardButton doBackup = QMessageBox::information(
		    this, tr("Clean up Files"), text, QMessageBox::Yes | QMessageBox::No | QMessageBox::Abort,
		    QMessageBox::Yes);

		if (doBackup == QMessageBox::Abort) {
			QMessageBox::information(this, tr("Clean up Files"), tr("Aborted."));
			return;
		}

		if (doBackup == QMessageBox::Yes) {
			QDir bkLoca;

			if (bkLoca.exists(backupFolder)) {
				QMessageBox::information(this, tr("Clean up Files"),
				                         tr("Aborted: `%1' already exist.").arg(backupFolder));
				return;
			}

			if (! bkLoca.mkpath(backupFolder)) {
				QMessageBox::information(this, tr("Clean up Files"),
				                         tr("Aborted: Cannot make dir `%1'.").arg(backupFolder));
				return;
			}

			bkLoca = QDir(backupFolder);
			auto *bkProcess = new QProgressDialog(tr("Making Backup..."), "", 0, 0, this);
			bkProcess->setWindowModality(Qt::WindowModal);
			bkProcess->setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
			bkProcess->setMinimumDuration(0);
			bkProcess->setCancelButton(nullptr);
			bkProcess->setRange(0, tarcnt);
			bkProcess->setValue(0);
			QCoreApplication::processEvents();
			basDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

			for (const auto &conDirWho : basDir.entryInfoList()) {
				bkLoca.mkpath(conDirWho.fileName());
				copyPath(conDirWho.path() + QDir::separator() + conDirWho.fileName(),
				         bkLoca.path() + QDir::separator() + conDirWho.fileName());
				bkProcess->setValue(bkProcess->value() + 1);
				QCoreApplication::processEvents();
			}

			delete bkProcess;
		}

		auto *process = new QProgressDialog(tr("Cleaning"), "", 0, 0, this);
		process->setWindowModality(Qt::WindowModal);
		process->setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
		process->setMinimumDuration(0);
		process->setCancelButton(nullptr);
		process->setRange(0, 5);
		process->setValue(0);
		process->setLabelText(tr("Working on it..."));
		QCoreApplication::processEvents();
		process->setRange(0, tarcnt + 5);
		process->setValue(0);
		process->setModal(true);
		process->setLabelText(tr("Fetching Data..."));
		QCoreApplication::processEvents();
		QSet<QString> tarNameSet;
		QSet<QString> nameSet;
		QMap<QString, int> typeSet;
		QMap<QString, QString> origSet;
		QList<Task *> taskList = curContest->getTaskList();
		process->setValue(1);
		process->setLabelText(tr("Initing..."));
		QCoreApplication::processEvents();

		for (int i = 0; i < taskList.size(); i++) {
			QString taskName = taskList[i]->getSourceFileName();
			typeSet[taskName] = i;
			nameSet.insert(taskName);

			if (taskList[i]->getTaskType() == Task::AnswersOnly) {
				for (auto *j : taskList[i]->getTestCaseList()) {
					for (const auto &k : j->getInputFiles()) {
						QString temp = QFileInfo(k).completeBaseName();
						tarNameSet.insert(temp);
						origSet[temp] = taskName;
					}
				}
			} else if (taskList[i]->getTaskType() == Task::Communication ||
			           taskList[i]->getTaskType() == Task::CommunicationExec) {
				QStringList sourcePaths = taskList[i]->getSourceFilesPath();

				for (const auto &j : sourcePaths) {
					QString temp = QFileInfo(j).completeBaseName();
					tarNameSet.insert(temp);
					origSet[temp] = taskName;
				}
			} else {
				tarNameSet.insert(taskName);
				origSet[taskName] = taskName;
			}
		}

		process->setValue(5);
		process->setLabelText(tr("Now Cleaning..."));
		QCoreApplication::processEvents();
		basDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

		for (const auto &conDirWho : basDir.entryInfoList()) {
			QDir conDir(conDirWho.filePath());
			conDir.setFilter(QDir::Files | QDir::Hidden);

			for (const auto &proFilWho : conDir.entryInfoList()) {
				if (proFilWho.suffix().length() <= 0 || proFilWho.suffix().toUpper() == "EXE")
					QFile::remove(proFilWho.absoluteFilePath());
			}

			conDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

			for (const auto &proDirWho : conDir.entryInfoList()) {
				if (nameSet.contains(proDirWho.fileName())) {
					QDir proDir(proDirWho.filePath());
					proDir.setFilter(QDir::Files | QDir::Hidden);

					for (const auto &sorFilWho : proDir.entryInfoList()) {
						if (sorFilWho.suffix().length() > 0 && sorFilWho.suffix().toUpper() != "EXE")
							QFile::copy(sorFilWho.filePath(),
							            conDirWho.filePath() + QDir::separator() + sorFilWho.fileName());
					}
				}

				removePath(proDirWho.absoluteFilePath());
			}

			for (const auto &proName : nameSet) {
				conDir.mkpath(proName);
			}

			conDir.setFilter(QDir::Files | QDir::Hidden);

			for (const auto &proFilWho : conDir.entryInfoList()) {
				QString proFilName = proFilWho.fileName();
				QString proName = proFilName;
				proName.truncate(proName.lastIndexOf("."));

				if (tarNameSet.contains(proName)) {
					QString taskName = origSet[proName];
					int who = typeSet[taskName];
					int types = taskList[who]->getTaskType();

					if (types == Task::Traditional || types == Task::Interaction ||
					    types == Task::Communication || types == Task::CommunicationExec) {
						if (proFilName != taskList[who]->getInputFileName() &&
						    proFilName != taskList[who]->getOutputFileName())
							QFile::copy(proFilWho.filePath(), conDirWho.filePath() + QDir::separator() +
							                                      taskName + QDir::separator() + proFilName);
					} else if (types == Task::AnswersOnly) {
						if (proFilWho.suffix() == taskList[who]->getAnswerFileExtension())
							QFile::copy(proFilWho.filePath(), conDirWho.filePath() + QDir::separator() +
							                                      taskName + QDir::separator() + proFilName);
					}
				}

				QFile::remove(proFilWho.absoluteFilePath());
			}

			conDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

			for (const auto &proDirWho : conDir.entryInfoList()) {
				QDir proDir(proDirWho.filePath());
				proDir.setFilter(QDir::Files | QDir::Hidden);

				for (const auto &sorFilWho : proDir.entryInfoList())
					QFile::copy(sorFilWho.filePath(),
					            conDirWho.filePath() + QDir::separator() + sorFilWho.fileName());
			}

			process->setValue(process->value() + 1);
			QCoreApplication::processEvents();
		}

		delete process;
		text = tr("Finished.") + "<br>";
		QMessageBox::information(this, tr("Clean up Files"), text);
	} else {
		QMessageBox::information(this, tr("Clean up Files"), tr("Aborted"));
	}
}

void LemonLime::tabIndexChanged(int index) {
	if (index != 1) {
		judgeExtButtonFlip(false);
		ui->judgeAction->setEnabled(false);
		ui->judgeButton->setEnabled(false);
		ui->cleanupAction->setEnabled(false);
		ui->refreshAction->setEnabled(false);

		if (index == 2) {
			ui->statisticsBrowser->refresh();
		}
	} else {
		QList<QTableWidgetSelectionRange> selectionRange = ui->resultViewer->selectedRanges();

		if (! selectionRange.empty()) {
			ui->judgeAction->setEnabled(true);
			ui->judgeButton->setEnabled(true);
		} else {
			ui->judgeAction->setEnabled(false);
			ui->judgeButton->setEnabled(false);
		}

		judgeExtButtonFlip(curContest && ! curContest->getContestantList().isEmpty());
		ui->cleanupAction->setEnabled(true);
		ui->refreshAction->setEnabled(true);
	}
}

void LemonLime::moveUpTask() {
	QTreeWidgetItem *curItem = ui->summary->currentItem();

	if (! curItem)
		return;

	int index = ui->summary->indexOfTopLevelItem(curItem);
	curContest->swapTask(index - 1, index);
	ui->summary->setContest(curContest);
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	curItem = ui->summary->topLevelItem(index - 1);

	if (! curItem)
		curItem = ui->summary->topLevelItem(index);

	if (curItem)
		ui->summary->setCurrentItem(curItem);
}

void LemonLime::moveDownTask() {
	QTreeWidgetItem *curItem = ui->summary->currentItem();

	if (! curItem)
		return;

	int index = ui->summary->indexOfTopLevelItem(curItem);
	curContest->swapTask(index + 1, index);
	ui->summary->setContest(curContest);
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	curItem = ui->summary->topLevelItem(index + 1);

	if (! curItem)
		curItem = ui->summary->topLevelItem(index);

	if (curItem)
		ui->summary->setCurrentItem(curItem);
}

void LemonLime::viewerSelectionChanged() {
	QList<QTableWidgetSelectionRange> selectionRange = ui->resultViewer->selectedRanges();

	if (! selectionRange.empty()) {
		ui->judgeButton->setEnabled(true);
		ui->judgeAction->setEnabled(true);
	} else {
		ui->judgeButton->setEnabled(false);
		ui->judgeAction->setEnabled(false);
	}
}

void LemonLime::contestantDeleted() {
	judgeExtButtonFlip(curContest && ! curContest->getContestantList().isEmpty());
	ui->cleanupAction->setEnabled(true);
	ui->refreshAction->setEnabled(true);
}

void LemonLime::saveContest(const QString &fileName) {
	if (fileName.isEmpty())
		return;

	const QString target = QFileInfo(fileName).absoluteFilePath();

	bool projectTarget = ! projectFile.isEmpty() && target == QFileInfo(projectFile).absoluteFilePath();

	if (! projectTarget && QFileInfo::exists(target)) {
		QFile probe(target);

		if (probe.open(QFile::ReadOnly)) {
			const QJsonObject object = QJsonDocument::fromJson(probe.readAll()).object();
			projectTarget = DayProject::isProjectObject(object);
		}
	}

	if (projectTarget) {
		LOG("Refused to overwrite a project file:", target);
		ui->statusBar->showMessage(tr("Save Failed"), 1000);
		WARN(target, "Save Failed");
		return;
	}

	QFile file(target);

	if (! file.open(QFile::WriteOnly)) {
		QMessageBox::warning(this, tr("Error"), tr("Cannot open file %1").arg(target), QMessageBox::Close);
		ui->statusBar->showMessage(tr("Save Failed"), 1000);
		WARN(target, "Save Failed");
		return;
	}

	QApplication::setOverrideCursor(Qt::WaitCursor);
	QJsonObject out;
	curContest->writeToJson(out);
	file.write(QJsonDocument(out).toJson(QJsonDocument::Compact));
	QApplication::restoreOverrideCursor();
	ui->statusBar->showMessage(tr("Saved"), 1000);
}

void LemonLime::loadContest(const QString &filePath) {
	bool isProject = false;
	QFile probe(filePath);

	if (probe.open(QFile::ReadOnly)) {
		char firstChar;
		probe.peek(&firstChar, 1);

		if (firstChar == '[' || firstChar == '{') {
			QJsonParseError err;
			const QJsonObject obj = QJsonDocument::fromJson(probe.readAll(), &err).object();
			isProject = (err.error == 0 && DayProject::isProjectObject(obj));
		}
	}

	if (isProject) {
		loadProject(filePath);
		return;
	}

	if (QFileInfo(filePath).fileName().compare(QStringLiteral("contest.conf"), Qt::CaseInsensitive) == 0) {
		LOG("Refused to load a broken project file:", filePath);
		QMessageBox::warning(this, tr("Error"),
		                     tr("File %1 is broken").arg(QFileInfo(filePath).fileName()), QMessageBox::Close);
		return;
	}

	curProject = DayProject();
	projectFile.clear();
	curDayIndex = 0;
	loadDay(QFileInfo(filePath).absoluteFilePath());
}

void LemonLime::loadProject(const QString &path) {
	QFile file(path);

	if (! file.open(QFile::ReadOnly)) {
		QMessageBox::warning(this, tr("Error"),
		                     tr("Cannot open file %1").arg(QFileInfo(path).fileName()), QMessageBox::Close);
		return;
	}

	QJsonParseError err;
	const QJsonObject obj = QJsonDocument::fromJson(file.readAll(), &err).object();

	if (err.error != 0 || ! DayProject::isProjectObject(obj)) {
		QMessageBox::warning(this, tr("Error"),
		                     tr("File %1 is broken").arg(QFileInfo(path).fileName()), QMessageBox::Close);
		return;
	}

	curProject = DayProject::fromJson(obj);
	projectFile = QFileInfo(path).absoluteFilePath();
	curDayIndex = -1;

	chooseDay();
}

void LemonLime::migrateLayout() {
	const QString oldData = QStringLiteral("data");
	const QString oldSource = QStringLiteral("source");
	const QString newData = QStringLiteral("problem");
	const QString newSource = QStringLiteral("answers");

	if (QDir(oldSource).exists() && ! QDir(newSource).exists())
		QDir().rename(oldSource, newSource);

	if (QDir(oldData).exists() && ! QDir(newData).exists()) {
		QDir().mkpath(newData);

		for (const QFileInfo &fi : QDir(oldData).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
			if (fi.isDir()) {
				const QString target =
				    newData + QDir::separator() + fi.fileName() + QDir::separator() + QStringLiteral("data");
				QDir().mkpath(target);

				for (const QFileInfo &f : QDir(fi.absoluteFilePath()).entryInfoList(QDir::Files))
					QFile::copy(f.absoluteFilePath(), target + QDir::separator() + f.fileName());
			} else {
				QFile::copy(fi.absoluteFilePath(), newData + QDir::separator() + fi.fileName());
			}
		}

		removePath(oldData);
		LOG("Legacy data/ migrated to problem/");
	}

	const int repairedItems = repairBrokenConfiguration();

	if (repairedItems > 0) {
		saveContest(curFile);
		ui->statusBar->showMessage(tr("Repaired %1 broken configuration item(s)").arg(repairedItems), 5000);
	}
}

int LemonLime::repairBrokenConfiguration() {
	if (! curContest)
		return 0;

	const ContestRepairReport report = curContest->repairBrokenConfiguration();

	for (const QString &change : report.changes)
		LOG("Repaired configuration:", change);

	for (const QString &item : report.unresolved)
		WARN("Unresolved configuration item:", item);

	if (! report.changes.isEmpty())
		LOG("Repaired", report.changes.size(), "broken configuration item(s)");

	return report.changes.size();
}

void LemonLime::loadDay(const QString &filePath) {
	if (curContest)
		closeAction();

	auto *loaded = new Contest(this);
	const QString name = QFileInfo(filePath).fileName();

	auto abort = [this, loaded]() {
		delete loaded;
		curContest = nullptr;
		curFile.clear();
	};

	QFile file(filePath);

	if (! file.open(QFile::ReadOnly)) {
		QMessageBox::warning(this, tr("Error"), tr("Cannot open file %1").arg(name), QMessageBox::Close);
		abort();
		return;
	}
	if (file.peek(3).startsWith(QByteArrayLiteral("\xEF\xBB\xBF")))
		file.seek(3);
	char firstChar;
	file.peek(&firstChar, 1);
	if (firstChar == '[' || firstChar == '{') {
		QJsonParseError parseError;
		QJsonObject inObj(QJsonDocument::fromJson(file.readAll(), &parseError).object());
		if (parseError.error != 0) {
			QMessageBox::warning(this, tr("Error"),
			                     tr("File %1 is broken").arg(name) + "\n" + parseError.errorString() +
			                         "at position" + QString("%1").arg(parseError.offset),
			                     QMessageBox::Close);
			abort();
			return;
		}
		QApplication::setOverrideCursor(Qt::WaitCursor);
		loaded->setSettings(settings);
		if (loaded->readFromJson(inObj) == -1) {
			QApplication::restoreOverrideCursor();
			QMessageBox::warning(this, tr("Error"), tr("File %1 is broken").arg(name), QMessageBox::Close);
			abort();
			return;
		}
	} else {
		QDataStream _in(&file);
		unsigned checkNumber = 0;
		_in >> checkNumber;

		if (checkNumber != unsigned(MagicNumber)) {
			QMessageBox::warning(this, tr("Error"), tr("File %1 is broken").arg(name), QMessageBox::Close);
			abort();
			return;
		}

		quint16 checksum = 0;
		int len = 0;
		_in >> checksum >> len;
		char *raw = new char[len];
		_in.readRawData(raw, len);

		if (qChecksum(QByteArrayView(raw, static_cast<uint>(len))) != checksum) {
			delete[] raw;
			QMessageBox::warning(this, tr("Error"), tr("File %1 is broken").arg(name), QMessageBox::Close);
			abort();
			return;
		}

		QByteArray data(raw, len);
		delete[] raw;
		data = qUncompress(data);
		QDataStream in(data);
		QApplication::setOverrideCursor(Qt::WaitCursor);
		loaded->setSettings(settings);
		loaded->readFromStream(in);
	}

	curContest = loaded;
	const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
	curFile = absolutePath;
	QDir::setCurrent(QFileInfo(absolutePath).path());
	migrateLayout();
	QDir().mkdir(Settings::dataPath());
	QDir().mkdir(Settings::sourcePath());
	QDir().mkdir(Settings::importPath());
	QDir().mkpath(Settings::statementPath());
	Settings::ensureDistDirs();
	Settings::ensureDayFolderOrdering(QDir::currentPath());

	for (auto *task : curContest->getTaskList()) {
		const QString base = task->getSourceFileName().isEmpty() ? task->getProblemTitle()
		                                                        : task->getSourceFileName();
		Settings::ensureTaskDirs(base);

		if (task->getTaskType() == Task::Interaction)
			task->prepareInteraction();
	}

	ui->summary->setContest(curContest);
	ui->resultViewer->setContest(curContest);
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->setContest(curContest);
	ui->statisticsBrowser->refresh();
	ui->tabWidget->setVisible(true);
	resetDataWatcher();
	ui->closeAction->setEnabled(true);
	ui->openFolderAction->setEnabled(true);
	ui->saveAction->setEnabled(true);
	ui->addTasksAction->setEnabled(true);
	ui->addProblemTasksAction->setEnabled(true);
	ui->exportAction->setEnabled(true);
	ui->actionExportStatistics->setEnabled(true);
	ui->actionChangeContestName->setEnabled(true);
	ui->actionContestSettings->setEnabled(true);
	ui->actionNewDay->setEnabled(! projectFile.isEmpty());
	ui->actionRemoveDay->setEnabled(! projectFile.isEmpty());
	ui->actionRenameProject->setEnabled(true);
	ui->cleanupAction->setEnabled(false);
	ui->refreshAction->setEnabled(false);
	refreshDayMenu();

	if (projectFile.isEmpty())
		setWindowTitle(tr("LemonLime-ng - %1").arg(curContest->getContestTitle()));
	else
		setWindowTitle(
		    tr("LemonLime-ng - %1 / %2").arg(curProject.title, curContest->getContestTitle()));

	ui->tabWidget->setCurrentIndex(0);
	const QString dayTitle =
	    (projectFile.isEmpty() || curDayIndex < 0 || curDayIndex >= curProject.days.size())
	        ? QString()
	        : curProject.days[curDayIndex].title;
	const QString projectTitle =
	    projectFile.isEmpty() ? curContest->getContestTitle() : curProject.title;
	ui->statementEdit->setDayContext(curContest, QFileInfo(curFile).completeBaseName(), dayTitle,
	                                 projectTitle);
	ui->statementEdit->reload();
	ui->dataWidget->setSettings(settings);
	ui->dataWidget->setContest(curContest);
	ui->exportWidget->setDayFile(curFile);
	ui->exportWidget->setContest(curContest);
	ui->exportWidget->setDefaultStatementFile(ui->statementEdit->pdfFileName());
	QApplication::restoreOverrideCursor();
	LOG("Contest -", curContest->getContestTitle(), "loaded successfully");
}

void LemonLime::newContest(const QString &title, const QString &savingName, const QString &path) {
	Q_UNUSED(savingName)

	if (! QDir(path).exists() && ! QDir().mkpath(path)) {
		QMessageBox::warning(this, tr("Error"), tr("Cannot make contest path"), QMessageBox::Close);
		return;
	}

	if (curContest)
		closeAction();

	curProject = DayProject();
	curProject.title = title;
	projectFile = QDir(path).absoluteFilePath(QStringLiteral("contest.conf"));
	curDayIndex = -1;
	saveProjectFile();

	QStringList recentContest = settings->getRecentContest();
	recentContest.append(QDir::toNativeSeparators(projectFile));
	settings->setRecentContest(recentContest);
	LOG("New Contest -", title);

	chooseDay(true);
}

void LemonLime::newAction() {
	auto *dialog = new NewContestDialog(this);

	if (dialog->exec() == QDialog::Accepted) {
		newContest(dialog->getContestTitle(), dialog->getSavingName(), dialog->getContestPath());
	}

	delete dialog;
}

void LemonLime::closeAction() {
	ui->statementEdit->saveIfNeeded();
	saveContest(curFile);
	releaseDataWatcher();
	ui->summary->setContest(nullptr);
	ui->taskEdit->setEditTask(nullptr);
	ui->resultViewer->setContest(nullptr);
	ui->statisticsBrowser->setContest(nullptr);
	ui->exportWidget->setContest(nullptr);
	ui->dataWidget->setContest(nullptr);
	delete curContest;
	curContest = nullptr;
	ui->tabWidget->setCurrentIndex(0);
	ui->tabWidget->setVisible(false);
	ui->closeAction->setEnabled(false);
	ui->openFolderAction->setEnabled(false);
	ui->saveAction->setEnabled(false);
	ui->addTasksAction->setEnabled(false);
	ui->addProblemTasksAction->setEnabled(false);
	ui->exportAction->setEnabled(false);
	ui->actionExportStatistics->setEnabled(false);
	ui->actionChangeContestName->setEnabled(false);
	ui->actionContestSettings->setEnabled(false);
	ui->actionNewDay->setEnabled(false);
	ui->actionRemoveDay->setEnabled(false);
	ui->actionRenameProject->setEnabled(false);
	ui->menuDays->setEnabled(false);
	ui->cleanupAction->setEnabled(false);
	ui->refreshAction->setEnabled(false);
	setWindowTitle(tr("LemonLime-ng"));
}

void LemonLime::saveAction() { saveContest(curFile); }

void LemonLime::openFolderAction() { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::currentPath())); }

void LemonLime::loadAction() {
	auto *dialog = new OpenContestDialog(this);
	dialog->setRecentContest(settings->getRecentContest());
	QStringList recentContest = dialog->getRecentContest();

	if (dialog->exec() == QDialog::Accepted) {
		QString selectedContest = dialog->getSelectedContest();

		for (int i = 0; i < recentContest.size(); i++) {
			if (recentContest[i] == selectedContest) {
				recentContest.removeAt(i);
				break;
			}
		}

		recentContest.prepend(selectedContest);
		loadContest(selectedContest);
	}

	settings->setRecentContest(recentContest);
	delete dialog;
}

void LemonLime::getFiles(const QString &path, const QStringList &filters, QMap<QString, QString> &files) {
	QDir dir(path);

	if (! filters.isEmpty())
		dir.setNameFilters(filters);

	QFileInfoList list = dir.entryInfoList(QDir::Files);

	for (auto &i : list) {
		files.insert(i.completeBaseName(), i.fileName());
	}
}

void LemonLime::addTask(const QString &title, const QList<std::pair<QString, QString>> &testCases,
                        int fullScore, int timeLimit, int memoryLimit) {
	Task *newTask = new Task;
	newTask->setProblemTitle(title);
	newTask->setSourceFileName(title);
	newTask->setInputFileName(title + ".in");
	newTask->setOutputFileName(title + ".out");
	newTask->refreshCompilerConfiguration(settings);
	newTask->setAnswerFileExtension(settings->getDefaultOutputFileExtension());
	curContest->addTask(newTask);
	Settings::ensureTaskDirs(title);

	for (const auto &testCase : testCases) {
		auto *newTestCase = new TestCase;
		newTestCase->setFullScore(fullScore);
		newTestCase->setTimeLimit(timeLimit);
		newTestCase->setMemoryLimit(memoryLimit);
		newTestCase->addSingleCase(title + QDir::separator() + QStringLiteral("data") + QDir::separator() +
		                               testCase.first,
		                           title + QDir::separator() + QStringLiteral("data") + QDir::separator() +
		                               testCase.second);
		newTask->addTestCase(newTestCase);
	}
}

void LemonLime::addTaskWithScoreScale(const QString &title,
                                      const QList<std::pair<QString, QString>> &testCases, int sumScore,
                                      int timeLimit, int memoryLimit, Task::TaskType taskType,
                                      Task::ComparisonMode comparisonMode,
                                      const QString &interactorSource, const QString &graderSource,
                                      const QString &checkerSource) {
	Task *newTask = new Task;
	newTask->setProblemTitle(title);
	newTask->setSourceFileName(title);
	newTask->setInputFileName(title + ".in");
	newTask->setOutputFileName(title + ".out");
	newTask->refreshCompilerConfiguration(settings);
	newTask->setAnswerFileExtension(settings->getDefaultOutputFileExtension());
	curContest->addTask(newTask);
	Settings::ensureTaskDirs(title);

	newTask->setComparisonMode(comparisonMode);

	if (comparisonMode == Task::TestlibSpecialJudgeMode && ! checkerSource.isEmpty())
		newTask->setSpecialJudge(Settings::graderFilePath(title, QFileInfo(checkerSource).fileName()));

	if (taskType == Task::AnswersOnly) {
		newTask->setTaskType(Task::AnswersOnly);
	} else if (taskType == Task::Interaction) {
		newTask->setTaskType(Task::Interaction);

		if (! interactorSource.isEmpty()) {
			const QString name = QFileInfo(interactorSource).fileName();
			newTask->setInteractor(Settings::graderFilePath(title, name));
			newTask->setInteractorName(name);
		}

		if (! graderSource.isEmpty())
			newTask->setGrader(Settings::graderFilePath(title, QFileInfo(graderSource).fileName()));

		if (interactorSource.isEmpty() || graderSource.isEmpty())
			newTask->prepareInteraction();
	}

	int scorePer = sumScore / testCases.size();
	int scoreLos = sumScore - scorePer * testCases.size();

	for (int i = 0; i < testCases.size(); i++) {
		auto *newTestCase = new TestCase;
		newTestCase->setFullScore(scorePer + static_cast<int>(i < scoreLos));
		newTestCase->setTimeLimit(timeLimit);
		newTestCase->setMemoryLimit(memoryLimit);
		newTestCase->addSingleCase(title + QDir::separator() + QStringLiteral("data") + QDir::separator() +
		                               testCases[i].first,
		                           title + QDir::separator() + QStringLiteral("data") + QDir::separator() +
		                               testCases[i].second);
		newTask->addTestCase(newTestCase);
	}
}

auto LemonLime::compareFileName(const std::pair<QString, QString> &a, const std::pair<QString, QString> &b)
    -> bool {
	return (a.first.length() < b.first.length()) ||
	       (a.first.length() == b.first.length() && QString::localeAwareCompare(a.first, b.first) < 0);
}

void LemonLime::addTasksAction() {
	QStringList list = QDir(Settings::importPath()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	QSet<QString> nameSet;
	QList<Task *> taskList = curContest->getTaskList();

	for (auto &i : taskList) {
		nameSet.insert(i->getSourceFileName());
	}

	QStringList nameList;
	QList<QList<std::pair<QString, QString>>> testCases;

	for (int i = 0; i < list.size(); i++) {
		if (! nameSet.contains(list[i])) {
			QStringList filters;
			filters = settings->getInputFileExtensions();

			if (filters.isEmpty())
				filters << "in";

			for (int j = 0; j < filters.size(); j++) {
				filters[j] = QString("*.") + filters[j];
			}

			QMap<QString, QString> inputFiles;
			getFiles(Settings::importPath() + list[i], filters, inputFiles);
			filters = settings->getOutputFileExtensions();

			if (filters.isEmpty())
				filters << "out" << "ans";

			for (int j = 0; j < filters.size(); j++) {
				filters[j] = QString("*.") + filters[j];
			}

			QMap<QString, QString> outputFiles;
			getFiles(Settings::importPath() + list[i], filters, outputFiles);
			QList<std::pair<QString, QString>> cases;
			QStringList baseNameList = inputFiles.keys();

			for (int j = 0; j < baseNameList.size(); j++) {
				if (outputFiles.contains(baseNameList[j])) {
					cases.append(std::make_pair(inputFiles[baseNameList[j]], outputFiles[baseNameList[j]]));
				}
			}

			std::sort(cases.begin(), cases.end(), compareFileName);

			if (! cases.isEmpty()) {
				nameList.append(list[i]);
				testCases.append(cases);
			}
		}
	}

	if (nameList.isEmpty()) {
		QMessageBox::warning(this, tr("LemonLime"), tr("No task found"), QMessageBox::Ok);
		return;
	}

	auto *dialog = new AddTaskDialog(this);
	dialog->setSourceRoot(Settings::importPath());
	dialog->resize(dialog->sizeHint());

	for (int i = 0; i < nameList.size(); i++) {
		dialog->addTask(nameList[i], qMax(100, testCases[i].size()), settings->getDefaultTimeLimit(),
		                settings->getDefaultMemoryLimit());
	}

	if (dialog->exec() == QDialog::Accepted) {
		for (int i = 0; i < nameList.size(); i++) {
			const QString taskName = nameList[i];
			const QString targetDir =
			    Settings::dataPath() + taskName + QDir::separator() + QStringLiteral("data");
			QDir().mkpath(targetDir);

			for (const auto &item : testCases[i]) {
				const QStringList names{item.first, item.second};

				for (const QString &fileName : names) {
					const QString source =
					    Settings::importPath() + taskName + QDir::separator() + fileName;
					const QString target = targetDir + QDir::separator() + fileName;

					if (! QFile::exists(source))
						continue;

					if (QFile::exists(target))
						QFile::remove(target);

					if (! QFile::rename(source, target)) {
						QFile::copy(source, target);
						QFile::remove(source);
					}
				}
			}

			const Task::TaskType taskType = dialog->getTaskType(i);
			const Task::ComparisonMode comparisonMode = dialog->getComparisonMode(i);
			const QString interactorSource = dialog->getInteractorSource(i);
			const QString graderSource = dialog->getGraderSource(i);
			const QString checkerSource = dialog->getCheckerSource(i);

			QStringList gradersFiles;

			if (taskType == Task::Interaction)
				gradersFiles << interactorSource << graderSource;

			if (comparisonMode == Task::TestlibSpecialJudgeMode)
				gradersFiles << checkerSource;

			if (! gradersFiles.isEmpty()) {
				const QString gradersDir = Settings::dataPath() + Settings::gradersPath(taskName);
				QDir().mkpath(gradersDir);

				for (const QString &source : gradersFiles) {
					if (source.isEmpty() || ! QFile::exists(source))
						continue;

					const QString target = gradersDir + QFileInfo(source).fileName();

					if (QFileInfo(source).absoluteFilePath() == QFileInfo(target).absoluteFilePath())
						continue;

					if (QFile::exists(target))
						QFile::remove(target);

					if (! QFile::rename(source, target)) {
						QFile::copy(source, target);
						QFile::remove(source);
					}
				}
			}

			addTaskWithScoreScale(taskName, testCases[i], dialog->getFullScore(i), dialog->getTimeLimit(i),
			                      dialog->getMemoryLimit(i), taskType, comparisonMode, interactorSource,
			                      graderSource, checkerSource);

			QDir(Settings::importPath() + taskName).removeRecursively();
		}
	}

	ui->summary->setContest(curContest);
}

void LemonLime::addProblemTasksAction() {
	if (! curContest)
		return;

	QStringList inputFilters = settings->getInputFileExtensions();

	if (inputFilters.isEmpty())
		inputFilters << QStringLiteral("in");

	QStringList outputFilters = settings->getOutputFileExtensions();

	if (outputFilters.isEmpty())
		outputFilters << QStringLiteral("out") << QStringLiteral("ans");

	for (QString &filter : inputFilters)
		filter.prepend(QStringLiteral("*."));

	for (QString &filter : outputFilters)
		filter.prepend(QStringLiteral("*."));

	QMap<QString, Task *> tasksByDir;

	for (auto *task : curContest->getTaskList())
		tasksByDir.insert(task->getDirectoryName().toLower(), task);

	QStringList addedTasks;
	QMap<QString, int> addedCases;
	const QStringList taskDirs =
	    QDir(Settings::dataPath()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

	for (const QString &taskName : taskDirs) {
		const QString dataDir =
		    Settings::dataPath() + taskName + QDir::separator() + QStringLiteral("data");

		if (! QDir(dataDir).exists())
			continue;

		QMap<QString, QString> inputFiles;
		QMap<QString, QString> outputFiles;
		getFiles(dataDir, inputFilters, inputFiles);
		getFiles(dataDir, outputFilters, outputFiles);

		QList<std::pair<QString, QString>> cases;

		for (const QString &baseName : inputFiles.keys())
			if (outputFiles.contains(baseName))
				cases.append(std::make_pair(inputFiles.value(baseName), outputFiles.value(baseName)));

		std::sort(cases.begin(), cases.end(), compareFileName);

		if (cases.isEmpty())
			continue;

		Task *task = tasksByDir.value(taskName.toLower(), nullptr);

		if (! task) {
			task = new Task;
			task->setProblemTitle(taskName);
			task->setSourceFileName(taskName);
			task->setInputFileName(taskName + QStringLiteral(".in"));
			task->setOutputFileName(taskName + QStringLiteral(".out"));
			task->setAnswerFileExtension(settings->getDefaultOutputFileExtension());
			task->setDefaultTimeLimit(settings->getDefaultTimeLimit());
			task->setDefaultMemoryLimit(settings->getDefaultMemoryLimit());
			task->refreshCompilerConfiguration(settings);
			curContest->addTask(task);
			Settings::ensureTaskDirs(taskName);
			tasksByDir.insert(taskName.toLower(), task);
			addedTasks.append(taskName);

			const QString interactor = Settings::graderFilePath(taskName, taskName + QStringLiteral(".h"));
			const QString grader =
			    Settings::graderFilePath(taskName, taskName + QStringLiteral("_grader.cpp"));

			if (QFileInfo::exists(Settings::dataPath() + interactor) &&
			    QFileInfo::exists(Settings::dataPath() + grader)) {
				task->setTaskType(Task::Interaction);
				task->setInteractor(interactor);
				task->setInteractorName(taskName + QStringLiteral(".h"));
				task->setGrader(grader);
			}

			const int sumScore = qMax(100, cases.size());
			const int scorePer = sumScore / cases.size();
			const int scoreLos = sumScore - scorePer * cases.size();

			for (int i = 0; i < cases.size(); i++) {
				auto *testCase = new TestCase;
				testCase->setFullScore(scorePer + (i < scoreLos ? 1 : 0));
				testCase->setTimeLimit(settings->getDefaultTimeLimit());
				testCase->setMemoryLimit(settings->getDefaultMemoryLimit());
				testCase->addSingleCase(taskName + QDir::separator() + QStringLiteral("data") +
				                            QDir::separator() + cases[i].first,
				                        taskName + QDir::separator() + QStringLiteral("data") +
				                            QDir::separator() + cases[i].second);
				task->addTestCase(testCase);
			}

			continue;
		}

		QSet<QString> configured;

		for (auto *testCase : task->getTestCaseList())
			for (const QString &input : testCase->getInputFiles())
				configured.insert(QDir::cleanPath(QDir::fromNativeSeparators(input)));

		int score = settings->getDefaultFullScore();
		int timeLimit = task->getDefaultTimeLimit() > 0 ? task->getDefaultTimeLimit()
		                                                : settings->getDefaultTimeLimit();
		int memoryLimit = task->getDefaultMemoryLimit() > 0 ? task->getDefaultMemoryLimit()
		                                                   : settings->getDefaultMemoryLimit();

		if (! task->getTestCaseList().isEmpty()) {
			const TestCase *last = task->getTestCaseList().last();
			score = last->getFullScore();
			timeLimit = last->getTimeLimit();
			memoryLimit = last->getMemoryLimit();
		}

		int added = 0;

		for (const auto &item : cases) {
			const QString input = taskName + QDir::separator() + QStringLiteral("data") +
			                      QDir::separator() + item.first;
			const QString output = taskName + QDir::separator() + QStringLiteral("data") +
			                       QDir::separator() + item.second;

			if (configured.contains(QDir::cleanPath(QDir::fromNativeSeparators(input))))
				continue;

			auto *testCase = new TestCase;
			testCase->setFullScore(score);
			testCase->setTimeLimit(timeLimit);
			testCase->setMemoryLimit(memoryLimit);
			testCase->addSingleCase(input, output);
			task->addTestCase(testCase);
			configured.insert(QDir::cleanPath(QDir::fromNativeSeparators(input)));
			added++;
		}

		if (added > 0)
			addedCases.insert(taskName, added);
	}

	ui->summary->setContest(curContest);

	if (addedTasks.isEmpty() && addedCases.isEmpty()) {
		QMessageBox::information(this, tr("Configure Tasks from problem/"),
		                         tr("No task or data file needs to be added."));
		return;
	}

	QStringList lines;

	if (! addedTasks.isEmpty())
		lines.append(tr("Added tasks: %1").arg(addedTasks.join(QStringLiteral(", "))));

	for (auto it = addedCases.constBegin(); it != addedCases.constEnd(); ++it)
		lines.append(tr("Added %1 test case(s) to %2").arg(it.value()).arg(it.key()));

	QMessageBox::information(this, tr("Configure Tasks from problem/"), lines.join(QChar('\n')));
}

void LemonLime::exportResult() { ExportUtil::exportResult(this, curContest); }

void LemonLime::exportStatistics() { StatisticsBrowser::exportStatistics(this, curContest); }

void LemonLime::changeContestName() {
	if (! curContest) {
		QMessageBox::warning(this, tr("Rename Contest"), tr("No Contest Yet"));
		return;
	}

	bool confirmed = false;
	QString newName = QInputDialog::getText(this, tr("Rename Contest"), tr("Write the name you want."),
	                                        QLineEdit::Normal, tr("New Name"), &confirmed);

	if (! confirmed) {
		QMessageBox::warning(this, tr("Rename Contest"), tr("The name did not changes."));
		return;
	}

	curContest->setContestTitle(newName);

	if (! projectFile.isEmpty() && curDayIndex >= 0 && curDayIndex < curProject.days.size()) {
		curProject.days[curDayIndex].title = newName;
		saveProjectFile();
		refreshDayMenu();
	}

	if (projectFile.isEmpty())
		setWindowTitle(tr("LemonLime-ng - %1").arg(curContest->getContestTitle()));
	else
		setWindowTitle(
		    tr("LemonLime-ng - %1 / %2").arg(curProject.title, curContest->getContestTitle()));

	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	saveContest(curFile);
}

void LemonLime::saveProjectFile() {
	if (projectFile.isEmpty())
		return;

	QSaveFile file(projectFile);

	if (! file.open(QFile::WriteOnly)) {
		QMessageBox::warning(this, tr("Error"), tr("Cannot open file %1").arg(projectFile), QMessageBox::Close);
		return;
	}

	file.write(QJsonDocument(curProject.toJson()).toJson(QJsonDocument::Compact));

	if (! file.commit())
		QMessageBox::warning(this, tr("Error"), tr("Cannot write file %1").arg(projectFile),
		                     QMessageBox::Close);
}

void LemonLime::refreshDayMenu() {
	ui->menuDays->clear();
	dayActions.clear();

	if (projectFile.isEmpty() || curProject.days.isEmpty()) {
		ui->menuDays->setEnabled(false);
		return;
	}

	ui->menuDays->setEnabled(true);

	for (int i = 0; i < curProject.days.size(); i++) {
		QAction *action = ui->menuDays->addAction(curProject.days[i].title);
		action->setCheckable(true);
		action->setChecked(i == curDayIndex);
		connect(action, &QAction::triggered, this, [this, i]() { switchDay(i); });
		dayActions.append(action);
	}
}

void LemonLime::switchDay(int index) {
	if (index < 0 || index >= curProject.days.size() || index == curDayIndex)
		return;

	openDay(index);
}

void LemonLime::openDay(int index) {
	if (projectFile.isEmpty() || index < 0 || index >= curProject.days.size())
		return;

	curDayIndex = index;
	loadDay(curProject.dayPath(QFileInfo(projectFile).absolutePath(), index));
}

bool LemonLime::createDay(const QString &title, const QString &fileName) {
	if (projectFile.isEmpty() || fileName.isEmpty())
		return false;

	if (fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\'))) {
		QMessageBox::warning(this, tr("New Contest Day"),
		                     tr("The folder name cannot contain path separators."), QMessageBox::Close);
		return false;
	}

	const QDir root(QFileInfo(projectFile).absolutePath());
	const QString dayDir = root.absoluteFilePath(fileName);

	for (const DayEntry &existing : curProject.days) {
		if (QFileInfo(existing.file).path() == fileName) {
			QMessageBox::warning(this, tr("New Contest Day"),
			                     tr("A contest day with this folder already exists."), QMessageBox::Close);
			return false;
		}
	}

	if (! QDir().mkpath(dayDir)) {
		QMessageBox::warning(this, tr("Error"), tr("Cannot make contest path"), QMessageBox::Close);
		return false;
	}

	if (curContest)
		closeAction();

	DayEntry entry;
	entry.title = title;
	entry.file = fileName + QStringLiteral("/") + fileName + QStringLiteral(".conf");
	curProject.days.append(entry);
	saveProjectFile();
	curDayIndex = curProject.days.size() - 1;

	{
		QDir::setCurrent(dayDir);
		QDir().mkdir(Settings::dataPath());
		QDir().mkdir(Settings::sourcePath());
		auto *blank = new Contest(this);
		blank->setSettings(settings);
		blank->setContestTitle(title);
		curContest = blank;
		curFile = QDir::current().absoluteFilePath(fileName + QStringLiteral(".conf"));
		saveContest(curFile);
		curContest = nullptr;
		delete blank;
	}

	loadDay(root.absoluteFilePath(entry.file));
	return true;
}

bool LemonLime::chooseDay(bool preferNew) {
	if (projectFile.isEmpty())
		return false;

	while (true) {
		DayDialog dialog(this);
		dialog.setContext(QFileInfo(projectFile).absolutePath(), curProject.days, curDayIndex);

		if (preferNew)
			dialog.preferNewTab();

		preferNew = false;

		if (dialog.exec() != QDialog::Accepted)
			return false;

		if (dialog.isDeleteRequested()) {
			deleteDayAt(dialog.getSelectedIndex());
			continue;
		}

		if (dialog.isNewDay())
			return createDay(dialog.getDayTitle(), dialog.getDayFileName());

		openDay(dialog.getSelectedIndex());
		return true;
	}
}

void LemonLime::deleteDayAt(int index, bool deleteFiles) {
	if (projectFile.isEmpty() || index < 0 || index >= curProject.days.size())
		return;

	const DayEntry entry = curProject.days.at(index);
	const QDir root(QFileInfo(projectFile).absolutePath());
	const QString dayDir = root.absoluteFilePath(QFileInfo(entry.file).path());
	const bool wasCurrent = (index == curDayIndex);

	if (curContest && wasCurrent)
		closeAction();

	if (deleteFiles) {
		QDir dir(dayDir);

		if (dir.exists() && QDir::cleanPath(dayDir) != QDir::cleanPath(root.absolutePath()))
			dir.removeRecursively();
	}

	curProject.days.removeAt(index);
	saveProjectFile();

	if (curProject.days.isEmpty()) {
		curDayIndex = -1;
		refreshDayMenu();
		return;
	}

	if (wasCurrent) {
		curDayIndex = qBound(0, index, curProject.days.size() - 1);
		openDay(curDayIndex);
	} else {
		if (index < curDayIndex)
			curDayIndex -= 1;

		refreshDayMenu();
	}
}

void LemonLime::newDay() {
	if (projectFile.isEmpty()) {
		QMessageBox::warning(this, tr("New Contest Day"), tr("Please create or open a contest first"));
		return;
	}

	chooseDay(true);
}

void LemonLime::newDayAction() { newDay(); }

void LemonLime::removeDay() {
	if (projectFile.isEmpty() || curDayIndex < 0 || curDayIndex >= curProject.days.size())
		return;

	const DayEntry entry = curProject.days[curDayIndex];
	const QMessageBox::StandardButton ret = QMessageBox::question(
	    this, tr("Remove Contest Day"),
	    tr("Remove contest day \"%1\"?\n\nYes: also delete its files.\nNo: remove it from the "
	       "contest only.")
	        .arg(entry.title),
	    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);

	if (ret == QMessageBox::Cancel)
		return;

	deleteDayAt(curDayIndex, ret == QMessageBox::Yes);
}

void LemonLime::renameProject() {
	if (projectFile.isEmpty()) {
		changeContestName();
		return;
	}

	bool confirmed = false;
	const QString newName = QInputDialog::getText(this, tr("Rename Contest"),
	                                              tr("Write the name you want."), QLineEdit::Normal,
	                                              curProject.title, &confirmed);

	if (! confirmed || newName.trimmed().isEmpty())
		return;

	curProject.title = newName.trimmed();
	saveProjectFile();

	if (curContest)
		setWindowTitle(
		    tr("LemonLime-ng - %1 / %2").arg(curProject.title, curContest->getContestTitle()));

	refreshDayMenu();
}

void LemonLime::removeDayAction() { removeDay(); }

void LemonLime::renameProjectAction() { renameProject(); }

void LemonLime::aboutLemon() {
	QString text;
	text += "<h2>Project LemonLime-ng</h2>";
	text +=
	    "<h3>" +
	    tr("Version: %1").arg(QString(LEMON_VERSION_STRING) + QString(":") + QString(LEMON_VERSION_BUILD)) +
	    "</h3>";
	text += tr("This is a tiny judging environment for OI contest based on Project LemonPlus.") + "<br>";
	text += tr("Based on Project Lemon version 1.2 Beta by Zhipeng Jia, 2011") + "<br>";
	text += tr("Based on Project LemonPlus by Dust1404, 2019") + "<br>";
	text += tr("Update by iotang and Coelacanthus") + "<br><br>";
	text += tr("Build Info: %1").arg(QString(LEMON_BUILD_INFO_STR)) + "<br>";
	text += tr("Build Extra Info: %1").arg(QString(LEMON_BUILD_EXTRA_INFO_STR)) + "<br>";
	text += tr("Build Date: %1").arg(QString(__DATE__) + QString(", ") + QString(__TIME__)) + "<br>";
	text += tr("This program is under the <a href=\"http://www.gnu.org/licenses/gpl-3.0.html\">GPLv3</a> "
	           "license") +
	        "<br>";
	QMessageBox::about(this, tr("About LemonLime-ng"), text);
}

void LemonLime::actionManual() {
	QDesktopServices::openUrl(QUrl(QString("https://project-lemonlime.github.io/Project_LemonLime/")));
}

void LemonLime::actionMore() {
	QDesktopServices::openUrl(QUrl(QString("https://github.com/Project-LemonLime/Project_LemonLime")));
}
