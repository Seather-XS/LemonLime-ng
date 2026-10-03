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

#include <QWidget>

namespace Ui {
	class TaskEditWidget;
}

class Settings;
class Task;

class TaskEditWidget : public QWidget {
	Q_OBJECT

  public:
	explicit TaskEditWidget(QWidget *parent = nullptr);
	~TaskEditWidget();
	void changeEvent(QEvent *);
	void setEditTask(Task *);
	void setSettings(Settings *);

  private:
	Ui::TaskEditWidget *ui;
	Settings *settings{};
	Task *editTask;
	bool loadingTask{false}; ///< 正在把 Task 的值刷进控件，此时不反向写回
	void refreshWidgetState();
	void addSourceFiles(const QString &, const QString &);
	void addGraderFiles(const QString &, const QString &);
	void rmSourceFilesAt(int);
	void rmGraderFilesAt(int);
	void multiFilesRefresh();
	void refreshComparisonMode(int);
	void refreshComparisonSettingPage();
	void refreshGraderRoots();
	QString currentTaskName() const;
	QList<int> comparisonModes;

  private slots:
	void problemTitleChanged(const QString &);
	void setToTraditional(bool);
	void setToAnswersOnly(bool);
	void setToInteraction(bool);
	void sourceFileNameChanged(const QString &);
	void subFolderCheckChanged();
	void inputFileNameChanged(const QString &);
	void outputFileNameChanged(const QString &);
	void standardInputCheckChanged();
	void standardOutputCheckChanged();
	void comparisonModeChanged();
	void specialJudgeChanged();
	void interactorChanged(const QString &);
	void graderPathEditingFinished();
	void interactorNameChanged(const QString &);
	void graderChanged(const QString &);
	void refreshProblemTitle(const QString &);
	void refreshCompilerConfiguration();
	void compilerSelectionChanged();
	void configurationSelectionChanged();
	void answerFileExtensionChanged(const QString &);
	void addSourceFileClicked();
	void addGraderFileClicked();
	void rmSourceFileClicked();
	void rmGraderFileClicked();

  signals:
	void dataPathChanged();
};
