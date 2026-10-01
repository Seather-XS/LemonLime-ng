/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "contestant.h"

#include "base/LemonUtils.hpp"
#include "base/settings.h"
#include "core/contest.h"
#include <QCoreApplication>
#include <QDir>
#include <QTimeZone>
#include <utility>

Contestant::Contestant(QObject *parent) : QObject(parent) {}

auto Contestant::getContestantName() const -> const QString & { return contestantName; }

auto Contestant::getRegion() const -> const QString & { return region; }

auto Contestant::getSourceFolder() const -> QString {
	const QString base = Settings::sourcePath();

	if (region.isEmpty())
		return base + contestantName;

	return base + region + QDir::separator() + contestantName;
}

auto Contestant::getDisqualifyState() const -> DisqualifyState { return disqualifyState; }

auto Contestant::getDisqualifyMessage() const -> const QString & { return disqualifyMessage; }

bool Contestant::isDisqualified() const { return disqualifyState != NotDisqualified; }

bool Contestant::isIgnoreRulesJudged() const { return ignoreRulesJudged; }

void Contestant::setIgnoreRulesJudged(bool flag) { ignoreRulesJudged = flag; }

auto Contestant::disqualifiedText() -> QString {
	return QCoreApplication::translate("Contestant", "Test cancelled");
}

auto Contestant::getDisqualifyTitle() const -> QString {
	switch (disqualifyState) {
		case ViolationDisqualified:
			return tr("Violation");

		case NamingDisqualified:
			return tr("Naming rule violated");

		case NotDisqualified:
			break;
	}

	return QString();
}

auto Contestant::getDisqualifyHtml() const -> QString {
	const QString title = getDisqualifyTitle();
	const QString body = disqualifyMessage.toHtmlEscaped();

	if (title.isEmpty())
		return body;

	return QStringLiteral("<p><b><span style=\"font-size:large;\">%1</span></b></p><p>%2</p>")
	    .arg(title.toHtmlEscaped(), body);
}

auto Contestant::getCheckJudged(int index) const -> bool { return checkJudged[index]; }

auto Contestant::getCompileState(int index) const -> CompileState { return compileState[index]; }

auto Contestant::getSourceFile(int index) const -> const QString & { return sourceFile[index]; }

auto Contestant::getCompileMessage(int index) const -> const QString & { return compileMesaage[index]; }

auto Contestant::getInputFiles(int index) const -> const QList<QStringList> & { return inputFiles[index]; }

auto Contestant::getResult(int index) const -> const QList<QList<ResultState>> & { return result[index]; }

auto Contestant::getMessage(int index) const -> const QList<QStringList> & { return message[index]; }

auto Contestant::getScore(int index) const -> const QList<QList<int>> & { return score[index]; }

auto Contestant::getTimeUsed(int index) const -> const QList<QList<int>> & { return timeUsed[index]; }

auto Contestant::getMemoryUsed(int index) const -> const QList<QList<qint64>> & { return memoryUsed[index]; }

auto Contestant::getJudingTime() const -> QDateTime { return judgingTime; }

void Contestant::setContestantName(const QString &name) { contestantName = name; }

void Contestant::setRegion(const QString &name) { region = name; }

void Contestant::setDisqualifyState(DisqualifyState state, const QString &message) {
	if (disqualifyState == state && disqualifyMessage == message)
		return;

	disqualifyState = state;
	disqualifyMessage = message;
}

void Contestant::setCheckJudged(int index, bool check) { checkJudged[index] = check; }

void Contestant::setCompileState(int index, CompileState state) { compileState[index] = state; }

void Contestant::setSourceFile(int index, const QString &fileName) { sourceFile[index] = fileName; }

void Contestant::setCompileMessage(int index, const QString &text) { compileMesaage[index] = text; }

void Contestant::setInputFiles(int index, const QList<QStringList> &files) { inputFiles[index] = files; }

void Contestant::setResult(int index, const QList<QList<ResultState>> &_result) { result[index] = _result; }

void Contestant::setMessage(int index, const QList<QStringList> &_message) { message[index] = _message; }

void Contestant::setScore(int index, const QList<QList<int>> &_score) { score[index] = _score; }

void Contestant::setTimeUsed(int index, const QList<QList<int>> &_timeUsed) { timeUsed[index] = _timeUsed; }

void Contestant::setMemoryUsed(int index, const QList<QList<qint64>> &_memoryUsed) {
	memoryUsed[index] = _memoryUsed;
}

void Contestant::setJudgingTime(QDateTime time) { judgingTime = std::move(time); }

void Contestant::addTask() {
	checkJudged.append(false);
	compileState.append(NoValidSourceFile);
	sourceFile.append("");
	compileMesaage.append("");
	inputFiles.append(QList<QStringList>());
	result.append(QList<QList<ResultState>>());
	message.append(QList<QStringList>());
	score.append(QList<QList<int>>());
	timeUsed.append(QList<QList<int>>());
	memoryUsed.append(QList<QList<qint64>>());
}

void Contestant::deleteTask(int index) {
	checkJudged.removeAt(index);
	compileState.removeAt(index);
	sourceFile.removeAt(index);
	compileMesaage.removeAt(index);
	inputFiles.removeAt(index);
	result.removeAt(index);
	message.removeAt(index);
	score.removeAt(index);
	timeUsed.removeAt(index);
	memoryUsed.removeAt(index);
}

void Contestant::swapTask(int a, int b) {
	if (a < 0 || a >= checkJudged.size())
		return;

	if (b < 0 || b >= checkJudged.size())
		return;

	checkJudged.swapItemsAt(a, b);
	compileState.swapItemsAt(a, b);
	sourceFile.swapItemsAt(a, b);
	compileMesaage.swapItemsAt(a, b);
	inputFiles.swapItemsAt(a, b);
	result.swapItemsAt(a, b);
	message.swapItemsAt(a, b);
	score.swapItemsAt(a, b);
	timeUsed.swapItemsAt(a, b);
	memoryUsed.swapItemsAt(a, b);
}

auto Contestant::getTaskScore(int index) const -> int {
	if (0 > index || index >= checkJudged.size())
		return -1;

	// 违规 / 命名不合规：整位选手每一道题都记 0 分（不编译不运行）。
	if (isDisqualified())
		return 0;

	if (! checkJudged[index])
		return -1;

	int total = 0;

	for (const auto &i : score[index]) {
		int minv = 1000000000;

		for (int j : i) {
			if (j < minv && j >= 0)
				minv = j;
		}

		if (minv == 1000000000)
			minv = 0;

		total += minv;
	}

	return total;
}

auto Contestant::getTotalScore() const -> int {
	// 违规 / 命名不合规都是全 0 分（依然参与排名），不按「没测过」处理。
	if (isDisqualified())
		return 0;

	if (checkJudged.empty())
		return -1;

	for (bool i : checkJudged) {
		if (! i)
			return -1;
	}

	int total = 0;

	for (int i = 0; i < score.size(); i++) {
		total += getTaskScore(i);
	}

	return total;
}

auto Contestant::getTotalUsedTime() const -> int {
	// 被判定取消的选手一题都没编译运行过，用时算 0（依然参与排名），
	// 不按「没测过」处理，否则表格里会显示成不可用。
	if (isDisqualified())
		return 0;

	if (checkJudged.empty())
		return -1;

	for (bool i : checkJudged) {
		if (! i)
			return -1;
	}

	int total = 0;

	for (const auto &i : timeUsed) {
		for (const auto &j : i) {
			for (int k : j) {
				if (k >= 0)
					total += k;
			}
		}
	}

	return total;
}
int Contestant::writeToJson(QJsonObject &out) {
	WRITE_JSON(out, contestantName);
	WRITE_JSON(out, region);
	// 判定结果是上次测试的结论，必须存盘，否则重新打开会变回「未测试」。
	int disqualifyStateValue = static_cast<int>(disqualifyState);
	WRITE_JSON(out, disqualifyStateValue);
	WRITE_JSON(out, disqualifyMessage);
	WRITE_JSON(out, ignoreRulesJudged);
	WRITE_JSON(out, checkJudged);
	WRITE_JSON(out, sourceFile);
	WRITE_JSON(out, compileMesaage);
	WRITE_JSON(out, inputFiles);
	WRITE_JSON(out, message);
	WRITE_JSON(out, score);
	WRITE_JSON(out, timeUsed);
	WRITE_JSON(out, memoryUsed);
	int judgingTime_date = judgingTime.date().toJulianDay();
	int judgingTime_time = judgingTime.time().msecsSinceStartOfDay();
	int judgingTime_timespec = judgingTime.timeSpec();
	WRITE_JSON(out, judgingTime_date);
	WRITE_JSON(out, judgingTime_time);
	WRITE_JSON(out, judgingTime_timespec);
	WRITE_JSON(out, compileState);
	WRITE_JSON(out, result);
	return 0;
}

int Contestant::readFromJson(const QJsonObject &in) {
	READ_JSON(in, contestantName);
	READ_JSON(in, region);
	// 恢复上次测试给出的违规 / 命名判定；此处不重新扫描代码，所以未测试过的选手
	// 仍然是 NotDisqualified（缺字段时默认为 0，兼容旧工程）。
	int disqualifyStateValue = int(NotDisqualified);
	READ_JSON(in, disqualifyStateValue);

	if (disqualifyStateValue < int(NotDisqualified) || disqualifyStateValue > int(NamingDisqualified))
		disqualifyStateValue = int(NotDisqualified);

	disqualifyState = static_cast<DisqualifyState>(disqualifyStateValue);
	READ_JSON(in, disqualifyMessage);
	READ_JSON(in, ignoreRulesJudged);
	READ_JSON(in, checkJudged);
	READ_JSON(in, sourceFile);
	READ_JSON(in, compileMesaage);
	READ_JSON(in, inputFiles);
	READ_JSON(in, message);
	READ_JSON(in, score);
	READ_JSON(in, timeUsed);
	READ_JSON(in, memoryUsed);
	int judgingTime_date = 0;
	int judgingTime_time = 0;
	int judgingTime_timespec = 0;
	READ_JSON(in, judgingTime_date);
	READ_JSON(in, judgingTime_time);
	READ_JSON(in, judgingTime_timespec);
	auto dt =
	    QDateTime(QDate::fromJulianDay(judgingTime_date), QTime::fromMSecsSinceStartOfDay(judgingTime_time));
	if (judgingTime_timespec == Qt::UTC) {
		dt.setTimeZone(QTimeZone::utc());
	} else {
		dt.setTimeZone(QTimeZone::systemTimeZone());
	}
	judgingTime = dt;
	READ_JSON(in, compileState);
	READ_JSON(in, result);
	return 0;
}

void Contestant::readFromStream(QDataStream &in) {
	in >> contestantName;
	in >> checkJudged;
	in >> sourceFile;
	in >> compileMesaage;
	in >> inputFiles;
	in >> message;
	in >> score;
	in >> timeUsed;
	// memoryUsed 之前存储为 int 三维数组，现在改为了 qint64，因此先读取旧结构，再逐层转换
	QList<QList<QList<int>>> oldMemoryUsed;
	in >> oldMemoryUsed;
	for (const auto &l1 : oldMemoryUsed) {
		QList<QList<qint64>> newL1;
		for (const auto &l2 : l1) {
			QList<qint64> newL2;
			for (int v : l2) {
				newL2.append(v);
			}
			newL1.append(newL2);
		}
		memoryUsed.append(newL1);
	}
	quint32 judgingTime_date = 0;
	quint32 judgingTime_time = 0;
	quint8 judgingTime_timespec = 0;
	in >> judgingTime_date;
	in >> judgingTime_time;
	in >> judgingTime_timespec;
	auto dt = QDateTime(QDate::fromJulianDay(judgingTime_date),
	                    QTime::fromMSecsSinceStartOfDay(static_cast<int>(judgingTime_time)));
	if (judgingTime_timespec == Qt::UTC) {
		dt.setTimeZone(QTimeZone::utc());
	} else {
		dt.setTimeZone(QTimeZone::systemTimeZone());
	}
	judgingTime = dt;
	int count = 0;
	int _count = 0;
	int __count = 0;
	int tmp = 0;
	in >> count;

	for (int i = 0; i < count; i++) {
		in >> tmp;
		compileState.append(CompileState(tmp));
	}

	in >> count;

	for (int i = 0; i < count; i++) {
		result.append(QList<QList<ResultState>>());
		in >> _count;

		for (int j = 0; j < _count; j++) {
			result[i].append(QList<ResultState>());
			in >> __count;

			for (int k = 0; k < __count; k++) {
				in >> tmp;
				result[i][j].append(ResultState(tmp));
			}
		}
	}
}
