/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "contest.h"

#include "base/LemonLog.hpp"
#include "base/LemonUtils.hpp"
#include "base/compiler.h"
#include "base/settings.h"
#include "core/contestant.h"
#include "core/judgingcontroller.h"
#include "core/judgingthread.h"
#include "core/naming.h"
#include "core/task.h"
#include "core/taskjudger.h"
#include "core/testcase.h"

#include <QDataStream>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QSet>
#include <algorithm>
#include <utility>

#define LEMON_MODULE_NAME "Contest"

namespace {
	QJsonObject ruleToJson(const ViolationRule &rule) {
		QJsonObject obj;
		obj.insert(QStringLiteral("kind"), rule.kind);
		obj.insert(QStringLiteral("text"), rule.text);
		return obj;
	}

	QVector<ViolationRule> rulesFromJson(const QJsonArray &arr) {
		QVector<ViolationRule> rules;

		for (const auto &item : arr) {
			const QJsonObject obj = item.toObject();
			rules.append({obj.value(QStringLiteral("kind")).toString(),
			              obj.value(QStringLiteral("text")).toString()});
		}

		return Violation::normalizeRules(rules);
	}

	/// 这位选手在某一题下的源文件（口径与引擎会编译的尽量一致）。
	QStringList sourceFilesOf(Contestant *contestant, Task *task) {
		QStringList result;

		// 提交答案题不编译选手源程序，不参与违规检测。
		if (task->getTaskType() == Task::AnswersOnly)
			return result;

		const QString base = contestant->getSourceFolder();
		QDir dir(task->getSubFolderCheck() ? base + QDir::separator() + task->getSourceFileName() : base);

		if (! dir.exists())
			return result;

		const QString sourceName = task->getSourceFileName();

		for (const auto &fi : dir.entryInfoList(QDir::Files)) {
			const QString name = fi.fileName();

			if (name == sourceName || name.startsWith(sourceName + QLatin1Char('.')))
				result.append(fi.absoluteFilePath());
		}

		return result;
	}

	// 题目的磁盘目录名是否可以直接拼进 problem/ 下用：空、带分隔符、`.` 与 `..` 都得挡掉，
	// 否则 removeRecursively() 会把整个 problem/ 甚至更上层的东西删掉。
	bool isSafeTaskDirectoryName(const QString &name) {
		if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
			return false;

		return ! name.contains(QLatin1Char('/')) && ! name.contains(QLatin1Char('\\'));
	}
} // namespace

Contest::Contest(QObject *parent) : QObject(parent) {
	violationRules = Violation::defaultRules();
	namingPattern = Naming::defaultPattern();
}

void Contest::setSettings(Settings *_settings) { settings = _settings; }

void Contest::copySettings(Settings &_settings) { _settings.copyFrom(settings); }

void Contest::setContestTitle(const QString &title) { contestTitle = title; }

auto Contest::getContestTitle() const -> const QString & { return contestTitle; }

auto Contest::getRegionEnabled() const -> bool { return regionEnabled; }

auto Contest::getViolationCheck() const -> bool { return violationCheck; }

auto Contest::getViolationRules() const -> const QVector<ViolationRule> & { return violationRules; }

auto Contest::getNamingCheck() const -> bool { return namingCheck; }

auto Contest::getNamingPattern() const -> const QString & { return namingPattern; }

void Contest::setRegionEnabled(bool enabled) { regionEnabled = enabled; }

void Contest::setViolationCheck(bool enabled) { violationCheck = enabled; }

void Contest::setViolationRules(const QVector<ViolationRule> &rules) {
	violationRules = Violation::normalizeRules(rules);
}

void Contest::setNamingCheck(bool enabled) { namingCheck = enabled; }

void Contest::setNamingPattern(const QString &pattern) { namingPattern = pattern.trimmed(); }

auto Contest::getStatementPdfName() const -> const QString & { return statementPdfName; }

void Contest::setStatementPdfName(const QString &name) { statementPdfName = name.trimmed(); }

void Contest::evaluateContestantRules() {
	const bool namingActive = namingCheck && ! namingPattern.isEmpty();
	const QVector<ViolationRule> rules = violationCheck ? Violation::normalizeRules(violationRules)
	                                                    : QVector<ViolationRule>();

	for (auto *contestant : contestantList) {
		// 「忽略限制重测」得到的成绩不被翻旧账（gengen-tuack 的语义）：这位选手一直算正常，
		// 直到他被普通重测（那时标记会被清掉，重新扫描代码）。
		if (contestant->isIgnoreRulesJudged()) {
			contestant->setDisqualifyState(Contestant::NotDisqualified, QString());
			continue;
		}

		// 命名限制比违规检测更靠前：文件夹名都不合规就不必再查代码内容。
		if (namingActive &&
		    ! Naming::matches(contestant->getContestantName(), contestant->getRegion(), namingPattern)) {
			contestant->setDisqualifyState(
			    Contestant::NamingDisqualified,
			    Naming::describe(contestant->getContestantName(), contestant->getRegion(), namingPattern));
			continue;
		}

		if (! rules.isEmpty()) {
			QStringList sources;

			for (int i = 0; i < taskList.size(); i++)
				sources += sourceFilesOf(contestant, taskList[i]);

			QString path;
			QVector<Violation::Match> matches;

			if (Violation::checkFiles(sources, rules, &path, &matches)) {
				contestant->setDisqualifyState(Contestant::ViolationDisqualified,
				                               Violation::describe(path, matches));
				continue;
			}
		}

		contestant->setDisqualifyState(Contestant::NotDisqualified, QString());
	}
}

auto Contest::getTask(int index) const -> Task * {
	if (0 <= index && index < taskList.size()) {
		return taskList[index];
	}

	return nullptr;
}

auto Contest::getTaskList() const -> const QList<Task *> & { return taskList; }

void Contest::swapTask(int a, int b) {
	if (0 <= a && a < taskList.size()) {
		if (0 <= b && b < taskList.size()) {
			taskList.swapItemsAt(a, b);
		}
	}

	for (auto &i : contestantList)
		i->swapTask(a, b);
}

auto Contest::getContestant(const QString &name) const -> Contestant * {
	if (contestantList.contains(name)) {
		return contestantList.value(name);
	}

	return nullptr;
}

auto Contest::getContestantList() const -> QList<Contestant *> { return contestantList.values(); }

auto Contest::getTotalTimeLimit() const -> int {
	int total = 0;

	for (auto *i : taskList) {
		QList<TestCase *> testCaseList = i->getTestCaseList();

		for (auto &j : testCaseList) {
			total += j->getTimeLimit() * j->getInputFiles().size();
		}
	}

	return total;
}

auto Contest::getTotalScore() const -> int {
	int total = 0;

	for (auto *i : taskList) {
		total += i->getTotalScore();
	}

	return total;
}

void Contest::addTask(Task *task) {
	taskList.append(task);
	connect(task, &Task::problemTitleChanged, this, &Contest::problemTitleChanged);
	emit taskAddedForContestant();
	emit taskAddedForViewer();
}

void Contest::deleteTask(int index) {
	if (0 <= index && index < taskList.size()) {
		// 题目在磁盘上的目录跟着一起删：problem/<题>/ 下的测试数据、校验器、交互库、
		// 数据生成器都只属于这一道题，留着只会变成垃圾。
		const QString taskName = taskList[index]->getDirectoryName();

		if (isSafeTaskDirectoryName(taskName)) {
			QDir taskDir(Settings::dataPath() + taskName);

			if (taskDir.exists()) {
				// 删不掉就是有句柄占着（QFileSystemWatcher、还开着的子进程、
				// 资源管理器里的预览等），记一条日志方便排查，别默默地失败。
				if (! taskDir.removeRecursively())
					WARN("Failed to remove the data folder of task", taskName);
				else
					LOG("Removed the data folder of task", taskName);
			}
		} else {
			LOG("Skip removing the data folder of task with unsafe name", taskName);
		}

		delete taskList[index];
		taskList.removeAt(index);
	}

	emit taskDeletedForContestant(index);
	emit taskDeletedForViewer(index);
}

void Contest::refreshContestantList() {
	// 选手目录：未启用赛区时是 source/<选手>/，启用赛区时是 source/<赛区>/<选手>/。
	QMap<QString, QString> nameList; // 选手名 → 赛区名（空表示未启用赛区）
	QDir sourceDir(Settings::sourcePath());

	if (regionEnabled) {
		for (const QString &region : sourceDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			QDir regionDir(sourceDir.path() + QDir::separator() + region);

			for (const QString &name : regionDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
				nameList.insert(name, region);
		}
	} else {
		for (const QString &name : sourceDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
			nameList.insert(name, QString());
	}

	QStringList curNameList = contestantList.keys();

	for (int i = 0; i < curNameList.size(); i++) {
		if (! nameList.contains(curNameList[i])) {
			delete contestantList[curNameList[i]];
			contestantList.remove(curNameList[i]);
		}
	}

	for (auto it = nameList.begin(); it != nameList.end(); ++it) {
		if (! contestantList.contains(it.key())) {
			auto *newContestant = new Contestant(this);
			newContestant->setContestantName(it.key());
			newContestant->setRegion(it.value());

			for (int j = 0; j < taskList.size(); j++) {
				newContestant->addTask();
			}

			contestantList.insert(it.key(), newContestant);
			connect(this, &Contest::taskAddedForContestant, newContestant, &Contestant::addTask);
			connect(this, &Contest::taskDeletedForContestant, newContestant, &Contestant::deleteTask);
		} else {
			contestantList.value(it.key())->setRegion(it.value());
		}
	}
}

void Contest::deleteContestant(const QString &name) {
	if (! contestantList.contains(name))
		return;

	delete contestantList[name];
	contestantList.remove(name);
}

void Contest::clearPath(const QString &curDir) {
	QDir dir(curDir);
	QStringList fileList = dir.entryList(QDir::Files);

	for (int i = 0; i < fileList.size(); i++) {
		if (! dir.remove(fileList[i])) {
#ifdef Q_OS_WIN32
			QProcess::execute(QString("attrib"), QStringList("-R") + QStringList(curDir + fileList[i]));
#else
			QProcess::execute(QString("chmod"), QStringList("+w") + QStringList(curDir + fileList[i]));
#endif
			dir.remove(fileList[i]);
		}
	}

	QStringList dirList = dir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);

	for (int i = 0; i < dirList.size(); i++) {
		clearPath(curDir + dirList[i] + QDir::separator());
		dir.rmdir(dirList[i]);
	}
}

void Contest::judge(const QVector<std::pair<Contestant *, int>> &judgingTasks, bool ignoreRules) {
	LOG("Start Judging");

	// 本轮涉及哪些选手：由它决定这些人这轮之后的"身份"。
	QSet<Contestant *> involved;

	for (auto [contestant, i] : judgingTasks)
		involved.insert(contestant);

	for (auto *contestant : involved) {
		if (ignoreRules) {
			// 忽略限制重测（gengen-tuack 语义）：本轮不跳过任何人，把他们标记成
			// 「成绩来自忽略限制重测」，之后重算判定时不再翻这段代码的旧账。
			contestant->setIgnoreRulesJudged(true);
			contestant->setDisqualifyState(Contestant::NotDisqualified, QString());
		} else {
			// 普通重测会重新检查代码，所以先撤掉之前的豁免。
			contestant->setIgnoreRulesJudged(false);
		}
	}

	if (ignoreRules)
		emit dialogAlert(tr("Ignore the restrictions and rejudge: the code content will NOT be checked "
		                    "this time"));

	// 两种重测都要重算一遍判定：被忽略限制重测的选手靠上面的标记豁免，
	// 其余选手（包括以前被忽略限制重测过的）都会按当前代码与设置重新检查。
	evaluateContestantRules();

	stopJudging = false;
	controller = new JudgingController(settings);

	// 被判定为违规 / 命名不合规的选手可以直接跳过：这里的信号让测试界面（日志）
	// 能写出「测试被取消 (...）」并提供详情入口。
	// 进度条的最大值是「本轮所有（选手，试题）的时限之和」，跳过的那些也要把这份工时
	// 补给测试界面，否则进度条永远走不满。
	QSet<QString> skipped;
	QHash<QString, int> skippedProgress;

	for (auto [contestant, i] : judgingTasks) {
		if (contestant->isDisqualified())
			skippedProgress[contestant->getContestantName()] += taskList[i]->getTotalTimeLimit();
	}

	// connect(controller, &JudgingController::judgeFinished, this, &Contest::judgeFinished);
	for (auto [contestant, i] : judgingTasks) {
		const QString contestantName = contestant->getContestantName();

		if (contestant->isDisqualified()) {
			if (! skipped.contains(contestantName)) {
				skipped.insert(contestantName);
				contestant->setJudgingTime(QDateTime::currentDateTime());
				emit contestantJudgingStart(contestantName);
				emit contestantSkipped(contestantName, contestant->getDisqualifyTitle(),
				                       contestant->getDisqualifyMessage(), skippedProgress.value(contestantName));
			}

			continue;
		}

		TaskJudger *taskJudger = new TaskJudger();
		connect(taskJudger, &TaskJudger::singleCaseFinished, this, &Contest::singleCaseFinished);
		connect(taskJudger, &TaskJudger::compileError, this, &Contest::compileError);
		// 每道题真正开跑时，先宣告选手（日志里「开始测试选手」必须在「开始测试试题」前面）。
		connect(taskJudger, &TaskJudger::judgingStarted, this,
		        [this, contestantName](const QString &problemTitle) {
			        emit contestantJudgingStart(contestantName);
			        emit taskJudgingStarted(problemTitle);
		        });
		connect(taskJudger, &TaskJudger::judgingFinished, this, &Contest::taskJudgingFinished);
		taskJudger->setTask(taskList[i]);
		taskJudger->setTaskId(i);
		taskJudger->setSettings(settings);
		taskJudger->setContestant(contestant);
		controller->addTask(taskJudger);
		/*
		connect(thread, &AssignmentThread::dialogAlert, this, &Contest::dialogAlert);
		connect(thread, &AssignmentThread::singleSubtaskDependenceFinished, this,
		        &Contest::singleSubtaskDependenceFinished);
		connect(this, &Contest::stopJudgingSignal, thread, &AssignmentThread::stopJudgingSlot);
		*/
		contestant->setJudgingTime(QDateTime::currentDateTime());
	}

	auto eventLoop = new QEventLoop();
	connect(controller, &JudgingController::judgeFinished, eventLoop, &QEventLoop::quit,
	        Qt::QueuedConnection);

	controller->start();

	eventLoop->exec();

	delete eventLoop;
	delete controller;
	controller = nullptr;
	std::atomic_thread_fence(std::memory_order_seq_cst);
	LOG("Judging Finished");
}

void Contest::judge(const QList<std::pair<QString, QVector<int>>> &list, bool ignoreRules) {
	QVector<std::pair<Contestant *, int>> judgingTasks;
	for (int i = 0; i < list.size(); i++) {
		auto contestant = contestantList.value(list[i].first);
		for (int j = 0; j < list[i].second.size(); j++)
			judgingTasks.push_back({contestant, list[i].second[j]});
	}
	judge(judgingTasks, ignoreRules);
}

void Contest::judgeAll() {
	QVector<std::pair<Contestant *, int>> judgingTasks;
	for (auto contestant : contestantList) {
		for (int i = 0; i < taskList.size(); i++) {
			judgingTasks.append({contestant, i});
		}
	}
	judge(judgingTasks);
}

void Contest::stopJudgingSlot() {
	stopJudging = true;
	QMetaObject::invokeMethod(controller, "stop");
}

void Contest::writeToJson(QJsonObject &out) {
	QString version = "1.0";

	WRITE_JSON(out, version);

	WRITE_JSON(out, contestTitle);

	WRITE_JSON(out, regionEnabled);
	WRITE_JSON(out, violationCheck);
	QJsonArray violationRulesJson;

	for (const auto &rule : violationRules)
		violationRulesJson.append(ruleToJson(rule));

	out.insert(QStringLiteral("violationRules"), violationRulesJson);
	WRITE_JSON(out, namingCheck);
	WRITE_JSON(out, namingPattern);
	WRITE_JSON(out, statementPdfName);

	QJsonArray tasks;

	for (const auto &i : taskList) {
		QJsonObject obj;
		i->writeToJson(obj);
		tasks.append(obj);
	}

	WRITE_JSON(out, tasks);

	QJsonArray contestants;

	for (const auto &i : contestantList) {
		QJsonObject obj;
		i->writeToJson(obj);
		contestants.append(obj);
	}

	WRITE_JSON(out, contestants);
}
int Contest::readFromJson(const QJsonObject &in) {
	QString version;

	// If there's no version number, consider as version 1.0

	if (READ_JSON(in, version) != -1) {
		if (version != "1.0")
			return -1;
	}

	READ_JSON(in, contestTitle);

	READ_JSON(in, regionEnabled);
	READ_JSON(in, violationCheck);

	if (in.contains(QStringLiteral("violationRules")))
		violationRules = rulesFromJson(in.value(QStringLiteral("violationRules")).toArray());

	READ_JSON(in, namingCheck);
	READ_JSON(in, namingPattern);
	// 老比赛文件里没有这一项：读不到就保持空，用默认的 statement。
	READ_JSON(in, statementPdfName);

	QJsonArray tasks;
	READ_JSON(in, tasks);

	// NOTE: taskList and contestantList are empty here because readFromJson is
	// only ever called once on a freshly constructed Contest (immediately after
	// construction, before any tasks or contestants are added). If this invariant
	// ever changes, qDeleteAll() must be called before clear() to avoid leaks.
	taskList.clear();
	for (const auto &task : tasks) {
		Task *newTask = new Task();
		if (newTask->readFromJson(task.toObject()) == -1)
			return -1;
		taskList.append(newTask);
	}

	QJsonArray contestants;
	READ_JSON(in, contestants);

	// See note above: contestantList is also empty at this point.
	contestantList.clear();
	for (const auto &contestant : contestants) {
		auto *newContestant = new Contestant();
		if (newContestant->readFromJson(contestant.toObject()) == -1)
			return -1;
		connect(this, &Contest::taskAddedForContestant, newContestant, &Contestant::addTask);
		connect(this, &Contest::taskDeletedForContestant, newContestant, &Contestant::deleteTask);
		contestantList.insert(newContestant->getContestantName(), newContestant);
	}
	return 0;
}
void Contest::readFromStream(QDataStream &in) {
	int count = 0;
	in >> contestTitle;
	in >> count;

	for (int i = 0; i < count; i++) {
		Task *newTask = new Task();
		newTask->readFromStream(in);
		newTask->refreshCompilerConfiguration(settings);
		taskList.append(newTask);
	}

	in >> count;

	for (int i = 0; i < count; i++) {
		auto *newContestant = new Contestant();
		newContestant->readFromStream(in);
		connect(this, &Contest::taskAddedForContestant, newContestant, &Contestant::addTask);
		connect(this, &Contest::taskDeletedForContestant, newContestant, &Contestant::deleteTask);
		contestantList.insert(newContestant->getContestantName(), newContestant);
	}
}
