/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include <QMainWindow>
#include <QtCore>

#include "core/dayproject.h"
#include "core/task.h"

namespace Ui {
	class LemonLime;
}

class Contest;
class Settings;
class OptionsDialog;

class LemonLime : public QMainWindow {
	Q_OBJECT

  public:
	explicit LemonLime(QWidget *parent = nullptr);
	~LemonLime();
	void changeEvent(QEvent *);
	void closeEvent(QCloseEvent *);
	int getSplashTime();
	void welcome();
	/// 自检（--check-close）用：按路径打开比赛日，不走文件对话框。返回是否真的载入了。
	bool openContestForCheck(const QString &fileName);

  private:
	Ui::LemonLime *ui;
	Contest *curContest;
	Settings *settings;
	QFileSystemWatcher *dataDirWatcher;
	/// 数据目录一动就炸出一堆信号（判题时文件一直在写），攒一下再重建监听。
	QTimer *dataWatcherTimer{nullptr};
	QString curFile;
	// 三层结构：比赛(Contest) → 比赛日(Day) → 试题(Task)。
	// projectFile 为工程根目录下 contest.conf 的绝对路径；以单个 .cdf 打开时为空。
	DayProject curProject;
	QString projectFile;
	int curDayIndex{0};
	QList<QAction *> dayActions;
	QSignalMapper *signalMapper;
	QMenu *TaskMenu;
	QList<QAction *> TaskList;
	QTimer autoSaveTimer;
	void judgeExtButtonFlip(bool);
	void loadUiLanguage();
	void insertWatchPath(const QString &, QFileSystemWatcher *);
	/// 真正重建数据目录监听（由 dataWatcherTimer 去抖后调用）。
	void rebuildDataWatcher();
	void newContest(const QString &, const QString &, const QString &);
	void saveContest(const QString &);
	void loadContest(const QString &);
	void loadDay(const QString &);
	void loadProject(const QString &);
	void migrateLayout();
	bool normalizeTestCasePaths();
	void refreshDayMenu();
	void saveProjectFile();
	void newDay();
	void removeDay();
	void deleteDayAt(int index, bool deleteFiles = true);
	void renameProject();
	void switchDay(int);
	bool chooseDay(bool preferNew = false);
	bool createDay(const QString &title, const QString &fileName);
	void openDay(int index);
	static void getFiles(const QString &, const QStringList &, QMap<QString, QString> &);
	void addTask(const QString &, const QList<std::pair<QString, QString>> &, int, int, int);
	void addTaskWithScoreScale(const QString &, const QList<std::pair<QString, QString>> &, int, int, int,
	                           Task::TaskType, Task::ComparisonMode, const QString &, const QString &,
	                           const QString &);
	static bool compareFileName(const std::pair<QString, QString> &, const std::pair<QString, QString> &);

  private slots:
	void summarySelectionChanged();
	void refreshSummary();
	void resetDataWatcher();
	/// 释放数据目录监听：删试题 / 删比赛日前必须调，否则目录被句柄占着删不掉。
	void releaseDataWatcher();
	void showOptionsDialog();
	void showContestSettingsDialog();
	void newDayAction();
	void removeDayAction();
	void renameProjectAction();
	void refreshButtonClicked();
	void cleanupButtonClicked();
	void tabIndexChanged(int);
	void moveUpTask();
	void moveDownTask();
	void viewerSelectionChanged();
	void contestantDeleted();
	void newAction();
	void saveAction();
	static void openFolderAction();
	void closeAction();
	void loadAction();
	void addTasksAction();
	void exportResult();
	void exportStatistics();
	void changeContestName();
	void aboutLemon();
	void actionManual();
	static void actionMore();

  signals:
	void dataPathChanged();
};
