/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

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

  private:
	Ui::LemonLime *ui;
	Contest *curContest;
	Settings *settings;
	QFileSystemWatcher *dataDirWatcher;
	QTimer *dataWatcherTimer{nullptr};
	QString curFile;
	DayProject curProject;
	QString projectFile;
	bool projectFileNewlyCreated{false};
	int curDayIndex{0};
	QList<QAction *> dayActions;
	QSignalMapper *signalMapper;
	QMenu *TaskMenu;
	QList<QAction *> TaskList;
	QTimer autoSaveTimer;
	void judgeExtButtonFlip(bool);
	void loadUiLanguage();
	void insertWatchPath(const QString &, QFileSystemWatcher *);
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
	void addProblemTasksAction();
	void exportResult();
	void exportStatistics();
	void changeContestName();
	void aboutLemon();
	void actionManual();
	static void actionMore();

  signals:
	void dataPathChanged();
};
