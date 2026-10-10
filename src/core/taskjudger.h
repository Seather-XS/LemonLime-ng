/*
 * SPDX-FileCopyrightText: 2021-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include "base/LemonType.hpp"
#include "core/judgingthread.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

class Contestant;
class Settings;
class Task;

class TaskJudger : public QObject {
	Q_OBJECT
  public:
	TaskJudger(QObject *parent = nullptr);
	// void setCheckRejudgeMode(bool);
	void setNeedRejudge(const QList<std::pair<int, int>> &);
	void setSettings(Settings *);
	void setTask(Task *);
	void setTaskId(int);
	void setContestant(Contestant *);
	Contestant *getContestant() const;
	CompileState getCompileState() const;
	// const QList< std::pair<int, int> >& getNeedRejudge() const;
	/// 源文件后缀在给定顺序里排第几（不在名单里的排最后）。
	/// 顺序来自**编译器自己声明的后缀列表**（如 g++ 的 `cpp;cc;cxx`）：
	/// 同一个语言声明多个后缀时，就按这个顺序取第一个存在的源文件。
	static int suffixRank(const QStringList &extensionOrder, const QString &fileName);
	/// 按 `extensionOrder` 给候选源文件排序（稳定排序，同后缀保持原顺序）。
	/// **跨语言的优先级由编译器列表的顺序决定**：评测时按编译器列表逐个试，
	/// 第一个能匹配到源文件并编译成功的编译器说了算（CCF 的 .c → .cpp → .pas
	/// 就是靠「gcc 排在 g++ 前面」实现的）。
	static QStringList orderSourceFiles(const QStringList &extensionOrder, const QStringList &files);

  private:
	// bool checkRejudgeMode;

	const QString commExecGrader = "grader";
	bool interpreterFlag{};
	Settings *settings{};
	Task *task{};
	Contestant *contestant;
	CompileState compileState;
	QString compileMessage;
	QString sourceFile;
	QString executableFile;
	QString arguments;
	QString diffPath;
	QString specialJudgeExecutable;
	QString specialJudgeError;
	double compilerTimeLimitRatio{};
	double compilerMemoryLimitRatio{};
	bool disableMemoryLimitCheck{};
	bool interpreterAsWatcher{};
	QProcessEnvironment environment;
	QList<int> overallStatus;
	QList<QList<int>> timeUsed;
	QList<QList<qint64>> memoryUsed;
	QList<QList<int>> score;
	QList<QList<ResultState>> result;
	QList<QStringList> message;
	QList<QStringList> inputFiles;

	QList<int> testCaseScore;
	bool isJudging;
	int taskId;
	bool traditionalTaskPrepare();
	void prepareSpecialJudge();
	void assign();
	void taskSkipped(const std::pair<int, int> &);
	void makeDialogAlert(QString);
	int judge();

	QTemporaryDir temporaryDir;

  public:
	void judgeIt();
  public slots:
	void stop();
  signals:
	void judgingStarted(QString);
	void judgingFinished();
	void dialogAlert(QString);
	void singleCaseFinished(QString, int, int, int, int, int, int, qint64);
	void singleSubtaskDependenceFinished(int, int, int);
	void compileError(int, int);
	void stopJudgingSignal();
};
