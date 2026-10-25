/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <QTableWidget>
#include <QVector>
#include <utility>

class Contest;

class ResultViewer : public QTableWidget {
	Q_OBJECT
  public:
	explicit ResultViewer(QWidget *parent = nullptr);
	void changeEvent(QEvent *);
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
	bool needsRefresh{false};
	QAction *deleteContestantAction;
	QAction *detailInformationAction;
	QAction *judgeSelectedAction;
	QAction *judgeIgnoreLimitsAction;
	QAction *deleteContestantKeyAction;
	void clearPath(const QString &);
	int taskColumnBase() const;
	QList<std::pair<QString, QVector<int>>> selectedJudgeList();

  private slots:
	void deleteContestant();
	void detailInformation();

  signals:
	void contestantDeleted();
};
