/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include "core/task.h"
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
	void setSourceRoot(const QString &);
	int getFullScore(int) const;
	int getTimeLimit(int) const;
	int getMemoryLimit(int) const;
	Task::TaskType getTaskType(int index) const;
	QString getInteractorSource(int index) const;
	QString getGraderSource(int index) const;
	Task::ComparisonMode getComparisonMode(int index) const;
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
