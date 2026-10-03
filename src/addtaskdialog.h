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

#include "core/task.h"
//
#include <QDialog>
#include <QList>
#include <QString>

namespace Ui {
	class AddTaskDialog;
}

class AddTaskDialog : public QDialog {
	Q_OBJECT

  public:
	explicit AddTaskDialog(QWidget *parent = nullptr);
	~AddTaskDialog();
	void addTask(const QString &, int, int, int);
	/// 导入数据所在的根目录（下面按题目名分子目录）；交互库 / 接口的候选从各题目录里找。
	void setSourceRoot(const QString &);
	int getFullScore(int) const;
	int getTimeLimit(int) const;
	int getMemoryLimit(int) const;
	/// 第 index 道题选择的题型。
	Task::TaskType getTaskType(int index) const;
	/// 第 index 道题选中的交互库（头文件）在导入目录里的绝对路径，没选时为空。
	QString getInteractorSource(int index) const;
	/// 第 index 道题选中的接口（grader 源码）在导入目录里的绝对路径，没选时为空。
	QString getGraderSource(int index) const;
	/// 第 index 道题选择的判题方式（全文匹配 / 自定义校验器）。
	Task::ComparisonMode getComparisonMode(int index) const;
	/// 自定义校验器选中的 .exe / 源码在导入目录里的绝对路径，没选时为空。
	QString getCheckerSource(int index) const;

  private:
	void refreshCandidates();
	void updateOptionalRows();

	Ui::AddTaskDialog *ui;
	QString sourceRoot;
	QList<int> fullScore;
	QList<int> timeLimit;
	QList<int> memoryLimit;
	QList<Task::TaskType> taskTypes;
	QList<QString> interactorSources;
	QList<QString> graderSources;
	QList<Task::ComparisonMode> comparisonChoices;
	QList<QString> checkerSources;
	bool loading{false};

  private slots:
	void taskBoxIndexChanged();
	void fullScoreChanged();
	void timeLimitChanged();
	void memoryLimitChanged();
	void taskTypeChanged();
	void comparisonChanged();
	void sourceSelectionChanged();
};
