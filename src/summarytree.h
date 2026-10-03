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

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMessageBox>
#include <QTreeWidget>

class Settings;
class Contest;

class SummaryTree : public QTreeWidget {
	Q_OBJECT
  public:
	explicit SummaryTree(QWidget *parent = nullptr);
	void changeEvent(QEvent *) override;
	void setContest(Contest *);
	void setSettings(Settings *);
	void contextMenuEvent(QContextMenuEvent *) override;

  private:
	int addCount;
	Contest *curContest;
	Settings *settings{};
	QAction *addTaskAction;
	QAction *addTestCaseAction;
	QAction *addTestCasesAction;
	QAction *addTaskKeyAction;
	QAction *addTestCaseKeyAction;
	QAction *deleteTaskAction;
	QAction *deleteTestCaseAction;
	QAction *deleteTaskKeyAction;
	QAction *deleteTestCaseKeyAction;
	QAction *ExtTestCaseModifierAction;

  private slots:
	void addTask();
	void addTestCase();
	void addTestCases();
	void deleteTask();
	void deleteTestCase();
	void selectionChanged();
	void itemChanged(QTreeWidgetItem *);
	void titleChanged(const QString &);
	void launchExtTestCaseModifier();

  signals:
	void taskChanged();
	/// 删试题之前先发这个信号，让外面的数据目录监听器松手：Windows 上
	/// QFileSystemWatcher 会给每个被监视目录开一个句柄，不释放就删不掉 problem/<题>/。
	void taskAboutToBeDeleted();
};
