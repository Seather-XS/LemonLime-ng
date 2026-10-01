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
#include <QDataStream>
#include <QDateTime>
#include <QJsonObject>
#include <QObject>

class Contestant : public QObject {
	Q_OBJECT
  public:
	/// 选手的比赛日级判定状态（违规检测 / 命名限制，见 gengen-tuack 语义）。
	enum DisqualifyState {
		NotDisqualified,      // 正常
		ViolationDisqualified, // 违规：所有试题 0 分、总分 -1
		NamingDisqualified     // 命名不合规：所有试题 0 分、总分 0
	};

	explicit Contestant(QObject *parent = nullptr);

	const QString &getContestantName() const;
	const QString &getRegion() const;
	/// 选手代码文件夹（未启用赛区时为 source/<选手>，启用时为 source/<赛区>/<选手>）。
	QString getSourceFolder() const;
	DisqualifyState getDisqualifyState() const;
	const QString &getDisqualifyMessage() const;
	bool isDisqualified() const;
	/// 当前成绩是否来自「忽略限制重测」（这类成绩不再参与违规 / 命名检查）。
	bool isIgnoreRulesJudged() const;
	/// 判定取消时的统一短文案（界面只写这一句，详情放在 (...) 里）。
	static QString disqualifiedText();
	/// 详情标题：违规 / 命名不符合限制。
	QString getDisqualifyTitle() const;
	/// 详情弹窗内容（已 HTML 转义）。
	QString getDisqualifyHtml() const;
	bool getCheckJudged(int) const;
	CompileState getCompileState(int) const;
	const QString &getSourceFile(int) const;
	const QString &getCompileMessage(int) const;
	const QList<QStringList> &getInputFiles(int) const;
	const QList<QList<ResultState>> &getResult(int) const;
	const QList<QStringList> &getMessage(int) const;
	const QList<QList<int>> &getScore(int) const;
	const QList<QList<int>> &getTimeUsed(int) const;
	const QList<QList<qint64>> &getMemoryUsed(int) const;
	QDateTime getJudingTime() const;
	int getTaskScore(int) const;
	int getTotalScore() const;
	int getTotalUsedTime() const;

	void setContestantName(const QString &);
	void setRegion(const QString &);
	void setDisqualifyState(DisqualifyState, const QString &message = QString());
	void setIgnoreRulesJudged(bool);
	void setCheckJudged(int, bool);
	void setCompileState(int, CompileState);
	void setSourceFile(int, const QString &);
	void setCompileMessage(int, const QString &);
	void setInputFiles(int, const QList<QStringList> &);
	void setResult(int, const QList<QList<ResultState>> &);
	void setMessage(int, const QList<QStringList> &);
	void setScore(int, const QList<QList<int>> &);
	void setTimeUsed(int, const QList<QList<int>> &);
	void setMemoryUsed(int, const QList<QList<qint64>> &);
	void setJudgingTime(QDateTime);

	int writeToJson(QJsonObject &);
	int readFromJson(const QJsonObject &);
	void readFromStream(QDataStream &);

  private:
	QString contestantName;
	QString region;
	DisqualifyState disqualifyState{NotDisqualified};
	QString disqualifyMessage;
	bool ignoreRulesJudged{false};
	QList<bool> checkJudged;
	QList<CompileState> compileState;
	QStringList sourceFile;
	QStringList compileMesaage;
	QList<QList<QStringList>> inputFiles;
	QList<QList<QList<ResultState>>> result;
	QList<QList<QStringList>> message;
	QList<QList<QList<int>>> score;
	QList<QList<QList<int>>> timeUsed;
	QList<QList<QList<qint64>>> memoryUsed;
	QDateTime judgingTime;

	// QList<TaskResult> taskResults;
  signals:

  public slots:
	void addTask();
	void deleteTask(int);
	void swapTask(int, int);
};
