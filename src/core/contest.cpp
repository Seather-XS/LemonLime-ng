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
#include <QHash>
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

	QString cleanedStoredPath(const QString &path) {
		return QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(path)));
	}

	QHash<QString, QString> indexTaskFiles(const QString &absoluteDir) {
		QHash<QString, QString> index;
		const QDir root(absoluteDir);

		if (! root.exists())
			return index;

		QStringList files;
		QStringList pending{absoluteDir};

		while (! pending.isEmpty()) {
			const QDir dir(pending.takeLast());

			for (const QFileInfo &entry : dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries,
			                                                QDir::Name)) {
				if (entry.isDir())
					pending.append(entry.absoluteFilePath());
				else if (entry.isFile())
					files.append(QDir::toNativeSeparators(root.relativeFilePath(entry.absoluteFilePath())));
			}
		}

		const QString dataPrefix = QStringLiteral("data") + QDir::separator();

		std::sort(files.begin(), files.end(), [&dataPrefix](const QString &left, const QString &right) {
			const bool leftInData = left.startsWith(dataPrefix);
			const bool rightInData = right.startsWith(dataPrefix);

			if (leftInData != rightInData)
				return leftInData;

			const int leftDepth = left.count(QDir::separator());
			const int rightDepth = right.count(QDir::separator());

			if (leftDepth != rightDepth)
				return leftDepth < rightDepth;

			return left < right;
		});

		for (const QString &file : files) {
			const QString key = QFileInfo(file).fileName().toLower();

			if (! index.contains(key))
				index.insert(key, file);
		}

		return index;
	}

	QStringList sourceFilesOf(Contestant *contestant, Task *task,
	                          QHash<QString, QFileInfoList> &directoryEntries) {
		QStringList result;

		if (task->getTaskType() == Task::AnswersOnly)
			return result;

		const QString base = contestant->getSourceFolder();
		const QString directoryPath =
		    task->getSubFolderCheck() ? base + QDir::separator() + task->getSourceFileName() : base;
		auto entries = directoryEntries.find(directoryPath);

		if (entries == directoryEntries.end()) {
			QDir dir(directoryPath);
			entries = directoryEntries.insert(directoryPath,
			                                  dir.exists() ? dir.entryInfoList(QDir::Files) : QFileInfoList{});
		}

		const QString sourceName = task->getSourceFileName();

		for (const auto &fi : entries.value()) {
			const QString name = fi.fileName();

			if (name == sourceName || name.startsWith(sourceName + QLatin1Char('.')))
				result.append(fi.absoluteFilePath());
		}

		return result;
	}

	bool isSafeTaskDirectoryName(const QString &name) {
		if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
			return false;

		return ! name.contains(QLatin1Char('/')) && ! name.contains(QLatin1Char('\\'));
	}
}

Contest::Contest(QObject *parent) : QObject(parent) {
	legacyViolationRules = Violation::defaultRules();
	namingPattern = Naming::defaultPattern();
}

void Contest::setSettings(Settings *_settings) { settings = _settings; }

void Contest::copySettings(Settings &_settings) { _settings.copyFrom(settings); }

void Contest::setContestTitle(const QString &title) { contestTitle = title; }

auto Contest::getContestTitle() const -> const QString & { return contestTitle; }

auto Contest::getRegionEnabled() const -> bool { return regionEnabled; }

auto Contest::getViolationCheck() const -> bool { return violationCheck; }

auto Contest::isViolationCompilerSelected(const QString &compilerName) const -> bool {
	if (! violationCompilersConfigured)
		return true;

	return violationCompilers.contains(compilerName);
}

auto Contest::isViolationCompilerEnabled(const QString &compilerName) const -> bool {
	return violationCheck && isViolationCompilerSelected(compilerName);
}

auto Contest::getNamingCheck() const -> bool { return namingCheck; }

auto Contest::getNamingPattern() const -> const QString & { return namingPattern; }

void Contest::setRegionEnabled(bool enabled) { regionEnabled = enabled; }

void Contest::setViolationCheck(bool enabled) { violationCheck = enabled; }

void Contest::setViolationCompilers(const QStringList &compilerNames) {
	violationCompilers.clear();

	for (const QString &name : compilerNames)
		if (! name.isEmpty() && ! violationCompilers.contains(name))
			violationCompilers.append(name);

	violationCompilersConfigured = true;
	legacyViolationRules.clear();
}

void Contest::setNamingCheck(bool enabled) { namingCheck = enabled; }

void Contest::setNamingPattern(const QString &pattern) { namingPattern = pattern.trimmed(); }

auto Contest::getStatementPdfName() const -> const QString & { return statementPdfName; }

void Contest::setStatementPdfName(const QString &name) { statementPdfName = name.trimmed(); }

void Contest::evaluateContestantRules() {
	const bool namingActive = namingCheck && ! namingPattern.isEmpty();

	QList<Compiler *> enabledCompilers;

	if (violationCheck && violationCompilersConfigured && settings) {
		for (auto *compiler : settings->getCompilerList())
			if (isViolationCompilerEnabled(compiler->getCompilerName()) &&
			    ! compiler->getViolationRules().isEmpty())
				enabledCompilers.append(compiler);
	}

	const bool legacyMode = violationCheck && ! violationCompilersConfigured;
	const QVector<ViolationRule> legacyRules =
	    legacyMode ? Violation::normalizeRules(legacyViolationRules) : QVector<ViolationRule>();

	for (auto *contestant : contestantList) {
		if (contestant->isIgnoreRulesJudged()) {
			contestant->setDisqualifyState(Contestant::NotDisqualified, QString());
			continue;
		}

		if (namingActive &&
		    ! Naming::matches(contestant->getContestantName(), contestant->getRegion(), namingPattern)) {
			contestant->setDisqualifyState(
			    Contestant::NamingDisqualified,
			    Naming::describe(contestant->getContestantName(), contestant->getRegion(), namingPattern));
			continue;
		}

		QHash<QString, QFileInfoList> directoryEntries;

		if (! legacyRules.isEmpty()) {
			QStringList sources;

			for (int i = 0; i < taskList.size(); i++)
				sources += sourceFilesOf(contestant, taskList[i], directoryEntries);

			QString path;
			QVector<Violation::Match> matches;

			if (Violation::checkFiles(sources, legacyRules, &path, &matches)) {
				contestant->setDisqualifyState(Contestant::ViolationDisqualified,
				                               Violation::describe(path, matches));
				continue;
			}
		} else if (! enabledCompilers.isEmpty()) {
			bool violated = false;

			for (auto *task : taskList) {
				const QStringList sources = sourceFilesOf(contestant, task, directoryEntries);

				if (sources.isEmpty())
					continue;

				Compiler *owner = nullptr;

				for (auto *compiler : enabledCompilers) {
					if (task->getCompilerConfiguration(compiler->getCompilerName()) ==
					    QStringLiteral("disable"))
						continue;

					for (const QString &extension : compiler->getSourceExtensions()) {
						const QString expected = task->getSourceFileName() + QLatin1Char('.') + extension;
						bool found = false;

						for (const QString &source : sources)
							if (QFileInfo(source).fileName() == expected) {
								found = true;
								break;
							}

						if (found) {
							owner = compiler;
							break;
						}
					}

					if (owner)
						break;
				}

				if (! owner)
					continue;

				QString path;
				QVector<Violation::Match> matches;

				if (Violation::checkFiles(sources, owner->getViolationRules(), &path, &matches)) {
					contestant->setDisqualifyState(Contestant::ViolationDisqualified,
					                               Violation::describe(path, matches));
					violated = true;
					break;
				}
			}

			if (violated)
				continue;
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
		const QString taskName = taskList[index]->getDirectoryName();

		if (isSafeTaskDirectoryName(taskName)) {
			QDir taskDir(Settings::dataPath() + taskName);

			if (taskDir.exists()) {
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
	QMap<QString, QString> nameList;
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

	QSet<Contestant *> involved;

	for (auto [contestant, i] : judgingTasks)
		involved.insert(contestant);

	for (auto *contestant : involved) {
		if (ignoreRules) {
			contestant->setIgnoreRulesJudged(true);
			contestant->setDisqualifyState(Contestant::NotDisqualified, QString());
		} else {
			contestant->setIgnoreRulesJudged(false);
		}
	}

	if (ignoreRules)
		emit dialogAlert(tr("Ignore the restrictions and rejudge: the code content will NOT be checked "
		                    "this time"));

	evaluateContestantRules();

	stopJudging = false;
	controller = new JudgingController(settings);

	QSet<QString> skipped;
	QHash<QString, int> skippedProgress;

	for (auto [contestant, i] : judgingTasks) {
		if (contestant->isDisqualified())
			skippedProgress[contestant->getContestantName()] += taskList[i]->getTotalTimeLimit();
	}

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

	if (violationCompilersConfigured) {
		WRITE_JSON(out, violationCompilers);
	} else {
		QJsonArray violationRulesJson;

		for (const auto &rule : legacyViolationRules)
			violationRulesJson.append(ruleToJson(rule));

		out.insert(QStringLiteral("violationRules"), violationRulesJson);
	}

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

	if (READ_JSON(in, version) != -1) {
		if (version != "1.0")
			return -1;
	}

	READ_JSON(in, contestTitle);

	READ_JSON(in, regionEnabled);
	READ_JSON(in, violationCheck);

	if (in.contains(QStringLiteral("violationCompilers"))) {
		violationCompilersConfigured = true;
		violationCompilers.clear();

		for (const auto &item : in.value(QStringLiteral("violationCompilers")).toArray()) {
			const QString name = item.toString();

			if (! name.isEmpty() && ! violationCompilers.contains(name))
				violationCompilers.append(name);
		}
	} else if (in.contains(QStringLiteral("violationRules"))) {
		legacyViolationRules = rulesFromJson(in.value(QStringLiteral("violationRules")).toArray());
	}

	READ_JSON(in, namingCheck);
	READ_JSON(in, namingPattern);
	READ_JSON(in, statementPdfName);

	QJsonArray tasks;
	READ_JSON(in, tasks);

	taskList.clear();
	for (const auto &task : tasks) {
		Task *newTask = new Task();
		if (newTask->readFromJson(task.toObject()) == -1)
			return -1;
		taskList.append(newTask);
	}

	QJsonArray contestants;
	READ_JSON(in, contestants);

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

ContestRepairReport Contest::repairBrokenConfiguration() {
	ContestRepairReport report;

	const QString dayRoot = QDir::currentPath();
	const QString dataRoot = Settings::dataPath();

	QStringList inputExtensions = settings ? settings->getInputFileExtensions() : QStringList();

	if (inputExtensions.isEmpty())
		inputExtensions << QStringLiteral("in");

	QStringList outputExtensions = settings ? settings->getOutputFileExtensions() : QStringList();

	if (outputExtensions.isEmpty())
		outputExtensions << QStringLiteral("out") << QStringLiteral("ans");

	for (Task *task : taskList) {
		QString taskName = task->getSourceFileName();

		if (taskName.isEmpty())
			taskName = task->getProblemTitle();

		if (taskName.isEmpty())
			continue;

		const QString taskDir = QDir(dayRoot).absoluteFilePath(dataRoot + taskName);
		const QHash<QString, QString> fileIndex = indexTaskFiles(taskDir);

		auto findFile = [&](const QString &stored, const QStringList &extensions) -> QString {
			const QString cleaned = cleanedStoredPath(stored);

			if (QFileInfo::exists(QDir(dayRoot).absoluteFilePath(dataRoot + cleaned)))
				return cleaned;

			const QString fileName = QFileInfo(cleaned).fileName();

			if (fileName.isEmpty())
				return {};

			auto hit = fileIndex.constFind(fileName.toLower());

			if (hit != fileIndex.constEnd())
				return QDir::toNativeSeparators(taskName + QDir::separator() + *hit);

			const QString baseName = QFileInfo(fileName).completeBaseName();

			for (const QString &extension : extensions) {
				hit = fileIndex.constFind((baseName + QLatin1Char('.') + extension).toLower());

				if (hit != fileIndex.constEnd())
					return QDir::toNativeSeparators(taskName + QDir::separator() + *hit);
			}

			return {};
		};

		auto repairOne = [&](const QString &stored, const QStringList &extensions) -> QString {
			if (stored.isEmpty())
				return stored;

			const QString fixed = findFile(stored, extensions);

			if (fixed.isEmpty()) {
				report.unresolved << QStringLiteral("%1: %2").arg(taskName, stored);
				return stored;
			}

			if (fixed != stored)
				report.changes << QStringLiteral("%1: %2 -> %3").arg(taskName, stored, fixed);

			return fixed;
		};

		for (TestCase *testCase : task->getTestCaseList()) {
			const QStringList inputs = testCase->getInputFiles();

			for (int i = 0; i < inputs.size(); i++)
				testCase->setInputFiles(i, repairOne(inputs[i], inputExtensions));

			const QStringList outputs = testCase->getOutputFiles();

			for (int i = 0; i < outputs.size(); i++)
				testCase->setOutputFiles(i, repairOne(outputs[i], outputExtensions));
		}

		int taskTimeLimit = task->getDefaultTimeLimit();
		int taskMemoryLimit = task->getDefaultMemoryLimit();

		if (taskTimeLimit <= 0) {
			int fallback = 0;

			for (TestCase *testCase : task->getTestCaseList())
				if (testCase->getTimeLimit() > 0) {
					fallback = testCase->getTimeLimit();
					break;
				}

			if (fallback <= 0 && settings)
				fallback = settings->getDefaultTimeLimit();

			if (fallback > 0) {
				task->setDefaultTimeLimit(fallback);
				taskTimeLimit = fallback;
				report.changes << QStringLiteral("%1: default time limit -> %2 ms").arg(taskName).arg(fallback);
			}
		}

		if (taskMemoryLimit <= 0) {
			int fallback = 0;

			for (TestCase *testCase : task->getTestCaseList())
				if (testCase->getMemoryLimit() > 0) {
					fallback = testCase->getMemoryLimit();
					break;
				}

			if (fallback <= 0 && settings)
				fallback = settings->getDefaultMemoryLimit();

			if (fallback > 0) {
				task->setDefaultMemoryLimit(fallback);
				taskMemoryLimit = fallback;
				report.changes << QStringLiteral("%1: default memory limit -> %2 MB").arg(taskName).arg(fallback);
			}
		}

		for (TestCase *testCase : task->getTestCaseList()) {
			if (testCase->getTimeLimit() <= 0 && taskTimeLimit > 0) {
				testCase->setTimeLimit(taskTimeLimit);
				report.changes << QStringLiteral("%1: test case time limit -> %2 ms")
				                      .arg(taskName)
				                      .arg(taskTimeLimit);
			}

			if (testCase->getMemoryLimit() <= 0 && taskMemoryLimit > 0) {
				testCase->setMemoryLimit(taskMemoryLimit);
				report.changes << QStringLiteral("%1: test case memory limit -> %2 MB")
				                      .arg(taskName)
				                      .arg(taskMemoryLimit);
			}
		}

		if (task->getAnswerFileExtension().isEmpty() && settings) {
			const QString extension = settings->getDefaultOutputFileExtension();

			if (! extension.isEmpty()) {
				task->setAnswerFileExtension(extension);
				report.changes << QStringLiteral("%1: answer file extension -> %2").arg(taskName, extension);
			}
		}

		const QString specialJudge = task->getSpecialJudge();

		if (! specialJudge.isEmpty()) {
			const QString cleaned = cleanedStoredPath(specialJudge);

			if (! QFileInfo::exists(QDir(dayRoot).absoluteFilePath(dataRoot + cleaned))) {
				const QString graderName = QFileInfo(cleaned).fileName();
				const QString graderPath = QDir(taskDir).absoluteFilePath(
				    QStringLiteral("graders") + QDir::separator() + graderName);

				if (QFileInfo::exists(graderPath)) {
					const QString fixed = Settings::graderFilePath(taskName, graderName);
					task->setSpecialJudge(fixed);
					report.changes << QStringLiteral("%1: special judge %2 -> %3")
					                      .arg(taskName, specialJudge, fixed);
				} else {
					report.unresolved << QStringLiteral("%1: special judge %2").arg(taskName, specialJudge);
				}
			}
		}

		if (task->getTaskType() == Task::Interaction && task->getInteractorName().isEmpty() &&
		    ! task->getInteractor().isEmpty()) {
			const QString name = QFileInfo(cleanedStoredPath(task->getInteractor())).fileName();
			task->setInteractorName(name);
			report.changes << QStringLiteral("%1: interactor name -> %2").arg(taskName, name);
		}
	}

	return report;
}
