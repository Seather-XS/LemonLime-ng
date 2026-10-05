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
//
#include "addcompilerwizard.h"
#include "addtaskdialog.h"
#include "base/LemonBase.hpp"
#include "base/LemonLog.hpp"
#include "base/LemonTranslator.hpp"
#include "base/compiler.h"
#include "base/settings.h"
#include "component/exportutil/exportutil.h"
#include "admission/admissionwidget.h"
#include "contestsettingsdialog.h"
#include "core/contest.h"
#include "core/contestant.h"
#include "core/task.h"
#include "core/testcase.h"
#include "daydialog.h"
#include "detaildialog.h"
#include "exportwidget.h"
#include "newcontestdialog.h"
#include "opencontestdialog.h"
#include "optionsdialog.h"
#include "statisticsbrowser.h"
#include "statementeditwidget.h"
#include "welcomedialog.h"
//
#include <QByteArrayView>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QTextBrowser>
#include <QUrl>
#include <algorithm>
#include <chrono>
//
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
	// 数据目录监听的去抖定时器：目录一变就打标记，400ms 后统一重建一次。
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
	// 删试题会让 problem/<题>/ 整个目录消失，删完得重新挂监听。
	connect(ui->summary, &SummaryTree::taskAboutToBeDeleted, this, &LemonLime::releaseDataWatcher);
	connect(ui->summary, &SummaryTree::taskChanged, this, &LemonLime::resetDataWatcher);
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
	// 题面 PDF 改了名字：导出选项卡里的题面下拉框跟着换默认项。
	connect(ui->statementEdit, &StatementEditWidget::pdfFileNameChanged, ui->exportWidget,
	        &ExportWidget::setDefaultStatementFile);
	connect(ui->newAction, &QAction::triggered, this, &LemonLime::newAction);
	connect(ui->openAction, &QAction::triggered, this, &LemonLime::loadAction);
	connect(ui->saveAction, &QAction::triggered, this, &LemonLime::saveAction);
	connect(ui->openFolderAction, &QAction::triggered, this, &LemonLime::openFolderAction);
	connect(ui->closeAction, &QAction::triggered, this, &LemonLime::closeAction);
	connect(ui->addTasksAction, &QAction::triggered, this, &LemonLime::addTasksAction);
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

void LemonLime::closeEvent(QCloseEvent * /*event*/) {
	ui->statementEdit->saveIfNeeded();
	// 准考证页也要：标题 / 测试时间可能刚好还在输入框里没落盘
	ui->admissionWidget->saveIfNeeded();

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
	// 目录内容一变（判题时数据文件一直在写）就会收到通知。
	// 重建 watcher 要遍历 problem/ 下所有目录，还会连带触发各个文件补全框重扫一遍，
	// 所以这里只打一个标记，攒 400ms 再统一做一次。
	if (dataWatcherTimer)
		dataWatcherTimer->start();
}

void LemonLime::releaseDataWatcher() {
	// QFileSystemWatcher 在 Windows 上是用 FindFirstChangeNotification() 实现的，
	// 每个被监视的目录都会挂一个句柄，占住目录不放：目录被监视着就删不掉（连父目录
	// 一起删也会失败）。删试题 / 删比赛日之前必须先把它拆掉。
	delete dataDirWatcher;
	dataDirWatcher = nullptr;
}

void LemonLime::rebuildDataWatcher() {
	releaseDataWatcher();
	dataDirWatcher = new QFileSystemWatcher(this);
	insertWatchPath(Settings::dataPath(), dataDirWatcher);
	// 只挂到去抖入口，避免每个文件的变动都全量重扫一遍。
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
	dialog->resetEditContest(curContest);

	if (dialog->exec() == QDialog::Accepted) {
		dialog->applyTo(curContest);
		// 违规 / 命名判定只在测试时进行，这里只刷新界面，不提前给选手定性。
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

	// curFile 以前只存文件名，完全依赖当前工作目录；工作目录一旦被切走
	// （例如误把工程文件当成比赛日载入），保存就会写进别的文件里。这里统一用绝对路径。
	const QString target = QFileInfo(fileName).absoluteFilePath();

	// 工程文件（contest.conf）只允许由 saveProjectFile() 写：比赛日的数据一旦写进去，
	// 整场比赛的配置（标题 + 比赛日列表）就没了。写入前做最后一道拦截。
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
	/* QByteArray data;
	QDataStream _out(&data, QIODevice::WriteOnly);
	curContest->writeToStream(_out);
	data = qCompress(data);
	QDataStream out(&file);
	out << unsigned(MagicNumber) << qChecksum(QByteArrayView(data))
	    << static_cast<int>(data.length()); // Qt 6+ uses qsizetype for length
	out.writeRawData(data.data(), data.length()); */
	QApplication::restoreOverrideCursor();
	ui->statusBar->showMessage(tr("Saved"), 1000);
}

void LemonLime::loadContest(const QString &filePath) {
	// 工程文件（contest.conf，含 days 数组）→ 载入第一个比赛日；
	// 否则把单个 .cdf 当作「只有一个比赛日」的工程，保持向后兼容。
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

	// 名字是 contest.conf 却没有 days 列表 —— 这是被写坏的比赛工程文件。
	// 绝不能把它当成「一个比赛日」载入：那样它自己会被当成比赛日文件重新写回去。
	if (QFileInfo(filePath).fileName().compare(QStringLiteral("contest.conf"), Qt::CaseInsensitive) == 0) {
		LOG("Refused to load a broken project file:", filePath);
		QMessageBox::warning(this, tr("Error"),
		                     tr("File %1 is broken").arg(QFileInfo(filePath).fileName()), QMessageBox::Close);
		return;
	}

	// 单个 .cdf / .conf 当作「只有一个比赛日」的工程载入，保持向后兼容。
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

	// 与 gengen-tuack 一致：打开比赛后弹窗选择 / 新建比赛日。
	chooseDay();
}

void LemonLime::migrateLayout() {
	// 旧 Lemon 布局：<比赛日>/data/<题>/... 与 <比赛日>/source/<选手>/
	// 新 gengen 布局：<比赛日>/problem/<题>/data/... 与 <比赛日>/answers/<赛区>/<选手>/
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
				// 每个题的数据目录整体搬到 problem/<题>/data/
				const QString target =
				    newData + QDir::separator() + fi.fileName() + QDir::separator() + QStringLiteral("data");
				QDir().mkpath(target);

				for (const QFileInfo &f : QDir(fi.absoluteFilePath()).entryInfoList(QDir::Files))
					QFile::copy(f.absoluteFilePath(), target + QDir::separator() + f.fileName());
			} else {
				// 平铺的文件（SPJ / 交互库等）路径不变，直接搬到 problem/ 下
				QFile::copy(fi.absoluteFilePath(), newData + QDir::separator() + fi.fileName());
			}
		}

		removePath(oldData);
		LOG("Legacy data/ migrated to problem/");
	}

	// 目录已经是新布局、但工程里还存着旧路径（如 <题>/<文件>）时，把路径归一化到
	// problem/<题>/data/<文件>，否则判定会报"找不到标准输入文件"。
	if (normalizeTestCasePaths())
		saveContest(curFile);
}

bool LemonLime::normalizeTestCasePaths() {
	if (! curContest)
		return false;

	bool changed = false;

	for (auto *task : curContest->getTaskList()) {
		// 数据目录以（创建后固定的）文件名为准，与 addTask / import 流程保持一致。
		QString taskName = task->getSourceFileName();

		if (taskName.isEmpty())
			taskName = task->getProblemTitle();

		if (taskName.isEmpty())
			continue;

		const QString prefix = taskName + QStringLiteral("/data/");

		auto fixOne = [&](const QString &stored) -> QString {
			if (stored.isEmpty() || stored.startsWith(prefix))
				return stored;

			const QString normalized = prefix + QFileInfo(stored).fileName();

			// 只有在新位置确实能找到文件时才改写，避免破坏用户自定义的路径。
			if (QFileInfo::exists(Settings::dataPath() + normalized))
				return normalized;

			return stored;
		};

		for (auto *testCase : task->getTestCaseList()) {
			const QStringList inputs = testCase->getInputFiles();

			for (int i = 0; i < inputs.size(); i++) {
				const QString fixed = fixOne(inputs[i]);

				if (fixed != inputs[i]) {
					testCase->setInputFiles(i, fixed);
					changed = true;
				}
			}

			const QStringList outputs = testCase->getOutputFiles();

			for (int i = 0; i < outputs.size(); i++) {
				const QString fixed = fixOne(outputs[i]);

				if (fixed != outputs[i]) {
					testCase->setOutputFiles(i, fixed);
					changed = true;
				}
			}
		}
	}

	if (changed)
		LOG("Test case paths normalized to problem/<task>/data/");

	return changed;
}

void LemonLime::loadDay(const QString &filePath) {
	if (curContest)
		closeAction();

	// 载入过程中任何一步失败，都不能把「空比赛」留在 curContest 里：curFile 还是上一次
	// 的路径，30 秒一次的自动保存会把空白内容写进那个文件（比赛配置就是这样丢的）。
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
	char firstChar;
	file.peek(&firstChar, 1);
	// Don't support RFC 7159, but support RFC 4627
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
	// curFile 存绝对路径：保存不再依赖当前工作目录，避免写进别的比赛日 / 工程里。
	const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
	curFile = absolutePath;
	QDir::setCurrent(QFileInfo(absolutePath).path());
	migrateLayout();
	QDir().mkdir(Settings::dataPath());
	QDir().mkdir(Settings::sourcePath());
	QDir().mkdir(Settings::importPath());
	// 每个比赛日一个 statement/ 目录：题面 markdown 与导出的 PDF 都放这里。
	QDir().mkpath(Settings::statementPath());
	// 导出物统一放 dist/ 下（dist/reports 成绩单与统计、dist/export 压缩包）；
	// 顺便清掉老工程根目录里残留的 reports/、export/。
	Settings::ensureDistDirs();

	// 补齐每个试题的标准子目录（data / down / graders / gen），顺便清掉老的 tests/
	for (auto *task : curContest->getTaskList()) {
		// 目录跟着（固定的）文件名走，与 addTask / import 流程一致。
		const QString base = task->getSourceFileName().isEmpty() ? task->getProblemTitle()
		                                                        : task->getSourceFileName();
		Settings::ensureTaskDirs(base);

		if (task->getTaskType() == Task::Interaction)
			task->prepareInteraction();
	}

	// 违规 / 命名判定不在载入时进行：未测试过的选手和普通选手看起来完全一样，
	// 只有真正开始测试时才扫描代码并给出「测试被取消」的结论。
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
		setWindowTitle(tr("LemonLime - %1").arg(curContest->getContestTitle()));
	else
		setWindowTitle(tr("LemonLime - %1 / %2").arg(curProject.title, curContest->getContestTitle()));

	ui->tabWidget->setCurrentIndex(0);
	// 题面：加载当前比赛日的 statement/statement.md，并把占位符要用的上下文交给它
	// （比赛日文件名 / 比赛日标题 / 比赛标题）。
	const QString dayTitle =
	    (projectFile.isEmpty() || curDayIndex < 0 || curDayIndex >= curProject.days.size())
	        ? QString()
	        : curProject.days[curDayIndex].title;
	const QString projectTitle =
	    projectFile.isEmpty() ? curContest->getContestTitle() : curProject.title;
	ui->statementEdit->setDayContext(curContest, QFileInfo(curFile).completeBaseName(), dayTitle,
	                                 projectTitle);
	ui->statementEdit->reload();
	// 导出：重新列出当前比赛日能打包的内容（默认题面取题面选项卡算出来的名字）
	ui->exportWidget->setDayFile(curFile);
	ui->exportWidget->setContest(curContest);
	ui->exportWidget->setDefaultStatementFile(ui->statementEdit->pdfFileName());
	// 准考证：列出 admission/ 下已有的名单（占位符用的上下文与题面完全一致）
	ui->admissionWidget->setContest(curContest);
	ui->admissionWidget->setDayContext(QFileInfo(curFile).completeBaseName(), dayTitle, projectTitle);
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

	// 三层结构：先只建立「比赛（工程）」本身，随后由「比赛日」窗口新建第一个比赛日。
	curProject = DayProject();
	curProject.title = title;
	projectFile = QDir(path).absoluteFilePath(QStringLiteral("contest.conf"));
	curDayIndex = -1;
	saveProjectFile();

	QStringList recentContest = settings->getRecentContest();
	recentContest.append(QDir::toNativeSeparators(projectFile));
	settings->setRecentContest(recentContest);
	LOG("New Contest -", title);

	// 与 gengen-tuack 一致：建完比赛后再弹窗选择 / 新建比赛日。
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
	// 还处于当前比赛日的工作目录，先把题面写回去。
	ui->statementEdit->saveIfNeeded();
	// 准考证页也一样：换比赛日 / 关比赛日之前先把标题、测试时间落盘
	ui->admissionWidget->saveIfNeeded();
	saveContest(curFile);
	// 关掉比赛日后可能整个比赛日目录都要被删（deleteDayAt），先把监听器拆掉。
	releaseDataWatcher();
	ui->summary->setContest(nullptr);
	ui->taskEdit->setEditTask(nullptr);
	ui->resultViewer->setContest(nullptr);
	ui->statisticsBrowser->setContest(nullptr);
	ui->exportWidget->setContest(nullptr);
	ui->admissionWidget->setContest(nullptr);
	delete curContest;
	curContest = nullptr;
	ui->tabWidget->setCurrentIndex(0);
	ui->tabWidget->setVisible(false);
	ui->closeAction->setEnabled(false);
	ui->openFolderAction->setEnabled(false);
	ui->saveAction->setEnabled(false);
	ui->addTasksAction->setEnabled(false);
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
	setWindowTitle(tr("LemonLime"));
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

	// 题型 / 判题方式就是导入时选的那个；交互题与自定义校验器顺带把选好的文件路径填上。
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

		// 没配全（或一个都没指定）的按原来的默认补上：生成 <题>.h 与 <题>_grader.cpp。
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
		newTestCase->addSingleCase(title + QDir::separator() + testCases[i].first,
		                           title + QDir::separator() + testCases[i].second);
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

			// 把 import 里的数据搬进 problem/<题>/data/（搬不动就拷贝）
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

			// 交互题带上交互库与接口，自定义校验器带上校验器文件：都要从 import/ 搬进
			// problem/<题>/graders/。
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

			// 导入完就把 import/ 下这道题的整个目录清掉（不管还剩什么文件）。
			QDir(Settings::importPath() + taskName).removeRecursively();
		}
	}

	ui->summary->setContest(curContest);
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

	// 三层结构下「更改标题」改的是当前比赛日的标题，同步写回工程文件。
	if (! projectFile.isEmpty() && curDayIndex >= 0 && curDayIndex < curProject.days.size()) {
		curProject.days[curDayIndex].title = newName;
		saveProjectFile();
		refreshDayMenu();
	}

	if (projectFile.isEmpty())
		setWindowTitle(tr("LemonLime - %1").arg(curContest->getContestTitle()));
	else
		setWindowTitle(tr("LemonLime - %1 / %2").arg(curProject.title, curContest->getContestTitle()));

	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	saveContest(curFile);
}

void LemonLime::saveProjectFile() {
	if (projectFile.isEmpty())
		return;

	// 覆盖前留一份上一次的内容：误写 / 写坏时还能救回来。
	if (QFile::exists(projectFile)) {
		const QString backup = projectFile + QStringLiteral(".bak");
		QFile::remove(backup);
		QFile::copy(projectFile, backup);
	}

	QFile file(projectFile);

	if (! file.open(QFile::WriteOnly)) {
		QMessageBox::warning(this, tr("Error"), tr("Cannot open file %1").arg(projectFile), QMessageBox::Close);
		return;
	}

	file.write(QJsonDocument(curProject.toJson()).toJson(QJsonDocument::Compact));
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

	// loadDay() 内部会先 closeAction() 保存当前比赛日，再切到新的工作目录。
	curDayIndex = index;
	loadDay(curProject.dayPath(QFileInfo(projectFile).absolutePath(), index));
}

bool LemonLime::createDay(const QString &title, const QString &fileName) {
	if (projectFile.isEmpty() || fileName.isEmpty())
		return false;

	// 比赛日目录直接建在比赛目录下的一级子目录里，名字里不允许出现路径分隔符。
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

	// 生成空的比赛日文件，再载入它。
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

		// 「打开」页签里点了删除：删完再让用户重新选。
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

	// 先保存并关闭当前比赛日，避免它仍持有被删目录中的文件句柄。
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
		// 单个 .cdf 的旧工程没有工程标题，退化为更改比赛标题。
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
		setWindowTitle(tr("LemonLime - %1 / %2").arg(curProject.title, curContest->getContestTitle()));

	refreshDayMenu();
}

void LemonLime::removeDayAction() { removeDay(); }

void LemonLime::renameProjectAction() { renameProject(); }

void LemonLime::aboutLemon() {
	QString text;
	text += "<h2>Project LemonLime</h2>";
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
	QMessageBox::about(this, tr("About LemonLime"), text);
}

void LemonLime::actionManual() {
	QDesktopServices::openUrl(QUrl(QString("https://project-lemonlime.github.io/Project_LemonLime/")));
}

void LemonLime::actionMore() {
	QDesktopServices::openUrl(QUrl(QString("https://github.com/Project-LemonLime/Project_LemonLime")));
}
