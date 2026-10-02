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

#include "base/LemonType.hpp"
#include <QDialog>
#include <QTextCursor>

class Contest;

namespace Ui {
	class JudgingDialog;
}

class JudgingDialog : public QDialog {
	Q_OBJECT

  public:
	explicit JudgingDialog(QWidget *parent = nullptr);
	~JudgingDialog();
	void setContest(Contest *);
	void judge(const QList<std::pair<QString, QVector<int>>> &, bool ignoreRules = false);
	void judgeAll();
	void reject();

  private slots:
	void stopJudgingSlot();
	void sendNotify(QString, QString);

  private:
	Ui::JudgingDialog *ui;
	Contest *curContest{};
	QTextCursor *cursor;
	bool stopJudging{};
	/// 上一行输出的选手名，避免同一选手重复输出「开始测试选手」
	QString lastContestant;

  public slots:
	void dialogAlert(const QString &);
	void singleCaseFinished(QString, int, int, int, int, int, int, qint64);
	void singleSubtaskDependenceFinished(int, int, int);
	void taskJudgingStarted(const QString &);
	void taskJudgedDisplay(const QString &, const QList<QList<int>> &, const int);
	void contestantJudgingStart(const QString &);
	void contestantJudgingFinished();
	void contestantJudgedDisplay(const QString &, const int, const int);
	void contestantSkipped(const QString &, const QString &, const QString &, int);
	void compileError(int, int);

  signals:
	void stopJudgingSignal();
};
