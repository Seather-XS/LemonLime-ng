/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 *                         2018-2019 Project LemonPlus, Dust1404
 *                         2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include "base/LemonType.hpp"
#include "core/violation.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#define MagicNumber 0x20111127

class Task;
class Settings;
class Contestant;
class JudgingController;

class Contest : public QObject {
	Q_OBJECT
  public:
	explicit Contest(QObject *parent = nullptr);
	void setSettings(Settings *);
	void copySettings(Settings &);
	void setContestTitle(const QString &);
	const QString &getContestTitle() const;

	/// —— 比赛日设置（语义见 gengen-tuack）——
	bool getRegionEnabled() const;
	bool getViolationCheck() const;
	const QVector<ViolationRule> &getViolationRules() const;
	bool getNamingCheck() const;
	const QString &getNamingPattern() const;
	void setRegionEnabled(bool);
	void setViolationCheck(bool);
	void setViolationRules(const QVector<ViolationRule> &);
	void setNamingCheck(bool);
	void setNamingPattern(const QString &);
	/// 按当前比赛日设置重算每位选手的违规 / 命名判定（改了设置不用重测）。
	void evaluateContestantRules();

	Task *getTask(int) const;
	void swapTask(int, int);
	const QList<Task *> &getTaskList() const;
	Contestant *getContestant(const QString &) const;
	QList<Contestant *> getContestantList() const;
	int getTotalTimeLimit() const;
	int getTotalScore() const;
	void addTask(Task *);
	void deleteTask(int);
	void refreshContestantList();
	void deleteContestant(const QString &);
	void writeToJson(QJsonObject &);
	void readFromStream(QDataStream &);
	int readFromJson(const QJsonObject &);

  private:
	QString contestTitle;
	bool regionEnabled{false};
	bool violationCheck{false};
	QVector<ViolationRule> violationRules;
	bool namingCheck{false};
	QString namingPattern;
	Settings *settings{};
	QList<Task *> taskList;
	QMap<QString, Contestant *> contestantList;
	bool stopJudging{};
	void judge(Contestant *);
	/// ignoreRules：忽略命名 / 违规限制重测（照常编译运行，不做判定）。
	void judge(const QVector<std::pair<Contestant *, int>> &, bool ignoreRules = false);
	void clearPath(const QString &);
	JudgingController *controller;

  public slots:
	void judge(const QList<std::pair<QString, QVector<int>>> &, bool ignoreRules = false);
	void judgeAll();
	// void judgeFinished();
	void stopJudgingSlot();

  signals:
	void taskAddedForContestant();
	void taskDeletedForContestant(int);
	void taskAddedForViewer();
	void taskDeletedForViewer(int);
	void problemTitleChanged();
	void dialogAlert(QString);
	void singleCaseFinished(QString, int, int, int, int, int, int, qint64);
	void singleSubtaskDependenceFinished(int, int, int);
	void taskJudgingStarted(QString);
	void taskJudgingFinished();
	void taskJudgedDisplay(const QString &, const QList<QList<int>> &, const int);
	void contestantJudgingStart(QString);
	void contestantJudgingFinished();
	void contestantJudgedDisplay(const QString &, const int, const int);
	/// 选手因违规 / 命名不合规被跳过测试（标题 + 详情）。
	void contestantSkipped(const QString &name, const QString &title, const QString &message);
	void compileError(int, int);
	void stopJudgingSignal();
};
