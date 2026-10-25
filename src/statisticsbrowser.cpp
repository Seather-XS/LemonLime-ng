/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "statisticsbrowser.h"
#include "ui_statisticsbrowser.h"
#include "base/LemonType.hpp"
#include "base/LemonUtils.hpp"
#include "base/settings.h"
#include "core/contest.h"
#include "core/contestant.h"
#include "core/task.h"
#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QtMath>

StatisticsBrowser::StatisticsBrowser(QWidget *parent) : QWidget(parent), ui(new Ui::StatisticsBrowser) {
	ui->setupUi(this);
	curContest = nullptr;

	refreshTimer = new QTimer(this);
	refreshTimer->setSingleShot(true);
	refreshTimer->setInterval(250);
	connect(refreshTimer, &QTimer::timeout, this, &StatisticsBrowser::refresh);
}

StatisticsBrowser::~StatisticsBrowser() { delete ui; }

void StatisticsBrowser::setContest(Contest *contest) { curContest = contest; }

auto StatisticsBrowser::getScoreNormalChart(const QMap<int, int> &scoreCount, int listSize, int totalScore)
    -> QString {
	QString buffer = "";
	long long overallScoreSum = 0;
	double scoreDiscrim = 0;
	double scoreStandardDevia = 0;
	int scoreTierPrefix = 0;
	int lastScoreTier = -1;
	int lastScoreTierNum = -1;

	for (auto i = scoreCount.constEnd(); i != scoreCount.constBegin();) {
		i--;
		int curScoreTier = i.key();
		int curScoreTierNum = i.value();

		if (curScoreTier < 0)
			continue;

		overallScoreSum += 1LL * curScoreTier * curScoreTierNum;

		if (lastScoreTier >= 0)
			scoreDiscrim += qLn(1 + 10.00 * (lastScoreTier - curScoreTier) / totalScore) *
			                (1.00 - 1.00 * lastScoreTierNum * curScoreTierNum / listSize / listSize);

		lastScoreTier = curScoreTier;
		lastScoreTierNum = curScoreTierNum;
	}

	double scoreAverage = 1.00 * overallScoreSum / listSize;
	buffer += "<table border=\"-1\">";
	buffer +=
	    QString(R"(<tr><th>%1</th><th>%2</th><th>%3</th><th colspan="2">%4</th><th colspan="2">%5</th></tr>)")
	        .arg(tr("Score"))
	        .arg(tr("Count"))
	        .arg(tr("Ratio"))
	        .arg(tr("Prefix"))
	        .arg(tr("Suffix"));

	for (auto i = scoreCount.constEnd(); i != scoreCount.constBegin();) {
		i--;
		int curScoreTier = i.key();
		int curScoreTierNum = i.value();
		scoreStandardDevia += qPow(curScoreTier - scoreAverage, 2) * curScoreTierNum;
		buffer += "<tr>";
		buffer += QString("<td align=\"right\"><nobr>%1 Pt</nobr></td>")
		              .arg(curScoreTier < 0 ? QString("N/A") : QString::number(curScoreTier));
		buffer += QString("<td align=\"right\"><nobr>%1</nobr></td>").arg(curScoreTierNum);
		buffer += QString("<td align=\"right\"><nobr>%1%</nobr></td>")
		              .arg(QString::number(100.00 * curScoreTierNum / listSize, 'f', 3));
		buffer += QString("<td align=\"right\"><nobr>%1</nobr></td>").arg(listSize - scoreTierPrefix);
		buffer += QString("<td align=\"right\"><nobr>%1%</nobr></td>")
		              .arg(QString::number(100.00 - 100.00 * scoreTierPrefix / listSize, 'f', 3));
		scoreTierPrefix += curScoreTierNum;
		buffer += QString("<td align=\"right\"><nobr>%1</nobr></td>").arg(scoreTierPrefix);
		buffer += QString("<td align=\"right\"><nobr>%1%</nobr></td>")
		              .arg(QString::number(100.00 * scoreTierPrefix / listSize, 'f', 3));
		buffer += "</tr>";
	}

	buffer += "</table>";
	scoreStandardDevia = qSqrt(scoreStandardDevia / listSize);
	scoreDiscrim = scoreDiscrim * scoreDiscrim;
	buffer += "<p>" + tr("Average") + " : " + QString::number(scoreAverage) + " / " +
	          QString::number(totalScore) + "</p>";
	buffer += "<p>" + tr("Standard Deviation") + " : " + QString::number(scoreStandardDevia) + "<p>";
	buffer += "<p>" + tr("Score Discrimination Power") + " : " + QString::number(scoreDiscrim) + "<p>";
	return buffer;
}

auto StatisticsBrowser::getTestcaseScoreChart(QList<TestCase *> testCaseList,
                                              QList<QList<QList<int>>> scoreList,
                                              QList<QList<QList<ResultState>>> resultList) -> QString {
	QString buffer = "";
	buffer += "<table border=\"-1\">";
	buffer +=
	    QString(
	        R"(<tr><th>%1</th><th>%2</th><th>%3</th><th colspan="2">%4</th><th colspan="2">%5</th><th colspan="2">%6</th><th>%7</th></tr>)")
	        .arg(tr("No."))
	        .arg(tr("Input"))
	        .arg(tr("Output"))
	        .arg(tr("Pure"))
	        .arg(tr("Far"))
	        .arg(tr("Lost"))
	        .arg(tr("Average"));

	for (int i = 0; i < testCaseList.length(); i++) {
		QStringList inFileList = testCaseList[i]->getInputFiles();
		QStringList outFileList = testCaseList[i]->getOutputFiles();
		int mxScore = testCaseList[i]->getFullScore();
		QList<int> miScoreRecord;
		QList<int> miStatRecord;

		for (int j = 0; j < scoreList.length(); j++) {
			miScoreRecord.append(mxScore);
			miStatRecord.append(2);
		}

		for (int j = 0; j < inFileList.length(); j++) {
			int cntFail = 0;
			int cntPati = 0;
			int cntSucc = 0;
			long long sumscore = 0;

			for (int k = 0; k < scoreList.length(); k++) {
				int score = 0;
				int statVal = 2;
				ResultState stat = WrongAnswer;

				if (scoreList[k].length() > i && scoreList[k][i].length() > j) {
					score = scoreList[k][i][j];
					stat = resultList[k][i][j];
				}

				if (stat == CorrectAnswer)
					cntSucc++, statVal = 2;
				else if (stat == PartlyCorrect)
					cntPati++, statVal = 1;
				else
					cntFail++, statVal = 0;

				sumscore += score;
				miScoreRecord[k] = qMin(miScoreRecord[k], score);
				miStatRecord[k] = qMin(miStatRecord[k], statVal);
			}

			buffer += "<tr>";
			buffer += "<td align=\"left\">" + QString("%1.%2").arg(i + 1).arg(j + 1) + "</td>";
			buffer += "<td align=\"left\">" + QString("%1").arg(inFileList[j]) + "</td>";
			buffer += "<td align=\"left\">" + QString("%1").arg(outFileList[j]) + "</td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(cntSucc) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * cntSucc / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(cntPati) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * cntPati / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(cntFail) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * cntFail / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1 / %2")
			              .arg(QString::number(1.00 * sumscore / scoreList.length(), 'f', 3))
			              .arg(mxScore) +
			          "</nobr></td>";
			buffer += "</tr>";
		}

		if (inFileList.length() > 1) {
			int sumCntFail = 0;
			int sumCntPati = 0;
			int sumCntSucc = 0;
			long long sumSumScore = 0;

			for (int j = 0; j < scoreList.length(); j++) {
				sumSumScore += miScoreRecord[j];

				if (miStatRecord[j] >= 2)
					sumCntSucc++;
				else if (miStatRecord[j] == 1)
					sumCntPati++;
				else
					sumCntFail++;
			}

			buffer += "<tr>";
			buffer += "<td align=\"left\">" + QString("%1 %2").arg(i + 1).arg("Overall") + "</td>";
			buffer +=
			    "<td align=\"left\">" + QString("%1 %2").arg(inFileList.length()).arg(tr("Files")) + "</td>";
			buffer +=
			    "<td align=\"left\">" + QString("%1 %2").arg(outFileList.length()).arg(tr("Files")) + "</td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(sumCntSucc) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * sumCntSucc / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(sumCntPati) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * sumCntPati / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(sumCntFail) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * sumCntFail / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1 / %2")
			              .arg(QString::number(1.00 * sumSumScore / scoreList.length(), 'f', 3))
			              .arg(mxScore) +
			          "</nobr></td>";
			buffer += "</tr>";
		}
	}

	buffer += "</table>";
	return buffer;
}

auto StatisticsBrowser::checkValid(QList<Task *> taskList, const QList<Contestant *> &contestantList)
    -> bool {
	for (auto *i : taskList) {
		for (auto *j : i->getTestCaseList()) {
			if (j->getInputFiles().length() != j->getOutputFiles().length())
				return false;
		}
	}

	for (auto *i : contestantList) {
		if (i->isDisqualified())
			continue;

		for (int j = 0; j < taskList.length(); j++) {
			QList<QList<int>> scoreList;
			QList<QList<ResultState>> resultList;
			QList<TestCase *> testCaseList;
			bool isJudged = false;

			try {
				scoreList = i->getScore(j);
				resultList = i->getResult(j);
				testCaseList = taskList[j]->getTestCaseList();
				isJudged = i->getCheckJudged(j);
			} catch (...) {
				return false;
			}

			if (! isJudged)
				return false;

			if (scoreList.length() != resultList.length())
				return false;

			if (scoreList.length() > 0 && resultList.length() > 0 && testCaseList.length() > 0) {
				if (scoreList.length() != testCaseList.length())
					return false;

				if (resultList.length() != testCaseList.length())
					return false;

				for (int k = 0; k < testCaseList.length(); k++) {

					if (scoreList[k].length() - (! testCaseList[k]->getDependenceSubtask().empty()) !=
					    testCaseList[k]->getInputFiles().length())
						return false;

					if (resultList[k].length() != testCaseList[k]->getInputFiles().length())
						return false;
				}
			}
		}
	}

	return true;
}

void StatisticsBrowser::refresh() {
	if (! isVisible()) {
		needsRefresh = true;
		return;
	}

	refreshTimer->stop();
	needsRefresh = false;

	if (! curContest) {
		ui->textBrowser->setHtml(tr("No contest yet"));
		return;
	}

	QList<Task *> taskList = curContest->getTaskList();
	QList<Contestant *> contestantList = curContest->getContestantList();

	if (taskList.empty()) {
		ui->textBrowser->setHtml(tr("No task yet"));
		return;
	}

	if (contestantList.empty()) {
		ui->textBrowser->setHtml(tr("No contestant yet"));
		return;
	}

	if (! checkValid(taskList, contestantList)) {
		ui->textBrowser->setHtml(tr("Some unhandled situation happened. May not all contestants are well "
		                            "judged, or not rejudged after changing testcases. Please refresh and "
		                            "rejudge."));
		return;
	}

	ui->textBrowser->setHtml(buildStatisticsHtml(curContest, contestantList));
}

void StatisticsBrowser::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);

	if (needsRefresh)
		refreshTimer->start();
}

auto StatisticsBrowser::buildStatisticsHtml(Contest *curContest, const QList<Contestant *> &contestantList,
                                            const QString &regionName) -> QString {
	QString buffer;
	QList<Task *> taskList = curContest->getTaskList();

	int totalScore = curContest->getTotalScore();
	buffer += "<html><head>";
	buffer += "<style type=\"text/css\">th, td {padding-left: 1em; padding-right: 1em;}</style>";
	buffer += "</head><body>";
	QString title = QString("%1 %2").arg(tr("Contest")).arg(curContest->getContestTitle());

	if (! regionName.isEmpty())
		title += QString(" (%1: %2)").arg(tr("Region")).arg(regionName);

	buffer += "<h1>" + title + "</h1>";
	buffer += "<h2>" + tr("Overall") + "</h2>";
	bool haveError = false;
	QMap<int, int> scoreCount;

	for (auto &i : contestantList) {
		int contestantTotalScore = 0;
		bool loss = false;

		for (int j = 0; j < taskList.size(); j++) {
			contestantTotalScore += i->getTaskScore(j);

			if (i->getTaskScore(j) < 0)
				haveError = true, loss = true;
		}

		if (! loss)
			scoreCount[contestantTotalScore]++;
		else
			scoreCount[-1]++;
	}

	if (haveError) {
		buffer += "<p style=\"font-size: large; color: red;\">" + tr("Warning: Judgement is not finished.") +
		          "</p><br>";
	}

	buffer += getScoreNormalChart(scoreCount, contestantList.size(), totalScore);
	buffer += "<br>";
	buffer += "<br>";
	buffer += "<h2>" + tr("Problems") + "</h2>";

	for (int i = 0; i < taskList.size(); i++) {
		buffer += "<h3>";
		buffer += QString("%1 %2: %3").arg(tr("Task")).arg(i + 1).arg(taskList[i]->getProblemTitle());
		buffer += "</h3>";
		int numberSubmitted = 0;
		int numberJudged = 0;
		QMap<int, int> cnts;
		QList<QList<QList<int>>> TestcaseScoreList;
		QList<QList<QList<ResultState>>> resultList;

		for (auto &j : contestantList) {
			cnts[j->getTaskScore(i)]++;

			if (j->isDisqualified())
				continue;

			numberJudged++;

			if (j->getCompileState(i) != NoValidSourceFile && j->getCompileState(i) != NoValidGraderFile)
				numberSubmitted++;

			TestcaseScoreList.append(j->getScore(i));
			resultList.append(j->getResult(i));
		}

		buffer += getScoreNormalChart(cnts, contestantList.size(), taskList[i]->getTotalScore());
		buffer += "<p>" + tr("Number of answer submitted") + " : " + QString::number(numberSubmitted) +
		          " / " + QString::number(numberJudged) + " (" +
		          QString::number(numberJudged == 0 ? 0.00
		                                            : 100.00 * numberSubmitted / numberJudged) +
		          "%)</p>";
		buffer += getTestcaseScoreChart(taskList[i]->getTestCaseList(), TestcaseScoreList, resultList);
		buffer += "<br>";
		buffer += "<br>";
	}

	buffer += "</body></html>";
	return buffer;
}

auto StatisticsBrowser::writeHtml(QWidget *widget, const QString &fileName, const QString &content) -> bool {
	QFile file(fileName);

	if (! file.open(QFile::WriteOnly)) {
		if (widget)
			QMessageBox::warning(widget, tr("LemonLime"),
			                     tr("Cannot open file %1").arg(QFileInfo(file).fileName()), QMessageBox::Ok);

		return false;
	}

	QApplication::setOverrideCursor(Qt::WaitCursor);
	QTextStream out(&file);
	out << content;
	QApplication::restoreOverrideCursor();
	return true;
}

void StatisticsBrowser::exportStatistics(QWidget *widget, Contest *curContest) {
	if (! curContest) {
		if (widget)
			QMessageBox::warning(widget, tr("LemonLime"), tr("No contest yet"), QMessageBox::Ok);

		return;
	}

	QList<Task *> taskList = curContest->getTaskList();
	QList<Contestant *> contestantList = curContest->getContestantList();

	if (taskList.empty()) {
		if (widget)
			QMessageBox::warning(widget, tr("LemonLime"), tr("No task yet"), QMessageBox::Ok);

		return;
	}

	if (contestantList.empty()) {
		if (widget)
			QMessageBox::warning(widget, tr("LemonLime"), tr("No contestant yet"), QMessageBox::Ok);

		return;
	}

	if (! checkValid(taskList, contestantList)) {
		if (widget)
			QMessageBox::warning(widget, tr("LemonLime"),
			                     tr("Some unhandled situation happened. May not all contestants are well "
			                        "judged, or not rejudged after changing testcases. Please refresh and "
			                        "rejudge."),
			                     QMessageBox::Ok);

		return;
	}

	const QString reportsDir = QDir::current().absoluteFilePath(Settings::reportsPath());

	if (! QDir().mkpath(reportsDir)) {
		if (widget)
			QMessageBox::warning(widget, tr("LemonLime"),
			                     tr("Cannot open file %1").arg(QDir::toNativeSeparators(reportsDir)),
			                     QMessageBox::Ok);

		return;
	}

	if (! writeHtml(widget, reportsDir + QDir::separator() + QStringLiteral("statistics.html"),
	                buildStatisticsHtml(curContest, contestantList)))
		return;

	if (curContest->getRegionEnabled()) {
		QMap<QString, QList<Contestant *>> byRegion;

		for (auto *contestant : contestantList) {
			if (! contestant->getRegion().isEmpty())
				byRegion[contestant->getRegion()].append(contestant);
		}

		for (auto it = byRegion.begin(); it != byRegion.end(); ++it) {
			const QString regionFile = QStringLiteral("statistics-") +
			                           Lemon::common::FileNameSafePart(it.key()) + QStringLiteral(".html");

			if (! writeHtml(widget, reportsDir + QDir::separator() + regionFile,
			                buildStatisticsHtml(curContest, it.value(), it.key())))
				return;
		}
	}

	if (widget)
		QMessageBox::information(widget, tr("LemonLime"), tr("Export is done"), QMessageBox::Ok);
}
