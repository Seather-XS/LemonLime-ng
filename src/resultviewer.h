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

#include <QTableWidget>
#include <QVector>
#include <utility>

class Contest;

class ResultViewer : public QTableWidget {
	Q_OBJECT
  public:
	explicit ResultViewer(QWidget *parent = nullptr);
	void changeEvent(QEvent *);
	/// 切到本选项卡时才把攒下的刷新补上（没显示时不重建表格）。
	void showEvent(QShowEvent *) override;
	void contextMenuEvent(QContextMenuEvent *);
	void setContest(Contest *);

  public slots:
	void refreshViewer();
	void judgeSelected();
	void judgeIgnoreLimits();
	void judgeAll();
	void judgeUnjudged();
	void judgeGrey();
	void judgeMagenta();

  private:
	Contest *curContest;
	/// 选项卡没显示时收到的刷新请求先攒着，切过来（showEvent）再重建。
	bool needsRefresh{false};
	QAction *deleteContestantAction;
	QAction *detailInformationAction;
	QAction *judgeSelectedAction;
	QAction *judgeIgnoreLimitsAction;
	QAction *deleteContestantKeyAction;
	void clearPath(const QString &);
	/// 试题成绩第一列的列号：Rank / Name [/ Region] / Total Score 之后（启用赛区时为 4，否则 3）。
	int taskColumnBase() const;
	/// 当前选中区域对应该测的 (选手名, 试题下标) 列表。
	QList<std::pair<QString, QVector<int>>> selectedJudgeList();

  private slots:
	void deleteContestant();
	void detailInformation();

  signals:
	void contestantDeleted();
};
