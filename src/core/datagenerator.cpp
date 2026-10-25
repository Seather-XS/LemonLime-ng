/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/datagenerator.h"
#include "base/ProcessUtil.hpp"
#include "core/task.h"
#include "core/testcase.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <utility>

namespace {
	QString twoDigits(int value) { return QStringLiteral("%1").arg(value, 2, 10, QChar('0')); }

	QString executableSuffix() {
#ifdef Q_OS_WIN32
		return QStringLiteral(".exe");
#else
		return {};
#endif
	}

	const QStringList &sourceSuffixes() {
		static const QStringList suffixes = {QStringLiteral(".cpp"), QStringLiteral(".cc"),
		                                     QStringLiteral(".cxx"), QStringLiteral(".c")};
		return suffixes;
	}

	QString findSource(const QString &directory, const QString &baseName) {
		for (const QString &suffix : sourceSuffixes()) {
			const QString path = directory + QChar('/') + baseName + suffix;

			if (QFileInfo::exists(path))
				return path;
		}

		return {};
	}

	QStringList extraSources(const QString &directory, const QString &baseName, const QString &otherBaseName) {
		QStringList names;

		for (const QString &suffix : sourceSuffixes()) {
			const QStringList files =
			    QDir(directory).entryList({QStringLiteral("*") + suffix}, QDir::Files, QDir::Name);

			for (const QString &name : files) {
				const QString base = name.left(name.length() - suffix.length());

				if (base.compare(baseName, Qt::CaseInsensitive) == 0 ||
				    base.compare(otherBaseName, Qt::CaseInsensitive) == 0)
					continue;

				names.append(name);
			}
		}

		return names;
	}

	QStringList splitArguments(const QString &text) {
		return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
	}

	QProcessEnvironment mergedEnvironment(const QProcessEnvironment &compilerEnvironment) {
		QProcessEnvironment environment = compilerEnvironment;
		const QProcessEnvironment system = QProcessEnvironment::systemEnvironment();

		for (const QString &key : system.keys()) {
			if (environment.contains(key)) {
				if (key.compare(QStringLiteral("PATH"), Qt::CaseInsensitive) != 0)
					continue;

#ifdef Q_OS_WIN32
				environment.insert(key, environment.value(key) + QChar(';') + system.value(key));
#else
				environment.insert(key, environment.value(key) + QChar(':') + system.value(key));
#endif
			} else {
				environment.insert(key, system.value(key));
			}
		}

		return environment;
	}

	struct ProcessOutcome {
		bool started{false};
		bool finished{false};
		int exitCode{-1};
		QByteArray output;
		QByteArray errorOutput;  
	};

	ProcessOutcome runProcess(const QString &program, const QStringList &arguments,
	                          const QString &workingDirectory, const QProcessEnvironment &environment,
	                          int timeLimit, const QString &inputFile, const QString &outputFile) {
		ProcessOutcome outcome;
		QProcess process;
		Lemon::common::suppressConsoleWindow(process);
		process.setProcessChannelMode(QProcess::SeparateChannels);
		process.setProcessEnvironment(environment);
		process.setWorkingDirectory(workingDirectory);

		if (! inputFile.isEmpty())
			process.setStandardInputFile(inputFile);

		if (! outputFile.isEmpty())
			process.setStandardOutputFile(outputFile);

		process.start(program, arguments);

		if (! process.waitForStarted(-1)) {
			outcome.errorOutput = process.errorString().toUtf8();
			return outcome;
		}

		outcome.started = true;
		outcome.finished = process.waitForFinished(timeLimit);

		if (! outcome.finished) {
			process.kill();
			process.waitForFinished(2000);
		}

		outcome.exitCode = process.exitCode();
		outcome.output = process.readAllStandardOutput();
		outcome.errorOutput = process.readAllStandardError();
		return outcome;
	}

	int clearFiles(const QString &directory) {
		QDir dir(directory);
		int removed = 0;

		for (const QFileInfo &info : dir.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)) {
			if (QFile::remove(info.absoluteFilePath()))
				removed++;
		}

		return removed;
	}

	QString firstLines(const QByteArray &text, int maxLength = 4000) {
		const QString trimmed = QString::fromLocal8Bit(text).trimmed();
		return trimmed.length() <= maxLength ? trimmed : trimmed.left(maxLength) + QStringLiteral(" …");
	}
} 

QString DataGenerator::generatorDirectory(const QString &taskDirectory) {
	return QDir::fromNativeSeparators(taskDirectory) + QStringLiteral("/generator");
}

QString DataGenerator::sandboxDirectory(const QString &taskDirectory) {
	return QDir::fromNativeSeparators(taskDirectory) + QStringLiteral("/sandbox");
}

QString DataGenerator::generatorTemplate() {
	return QString::fromUtf8(R"CPP(// gen.cpp -- data generator (written by LemonLime; edit freely).
//
// LemonLime runs `gen <sub_id> <test_id>` and stores stdout as one input file:
//   sub_id  : subtask id; 0 when subtasks are off, -1 for samples;
//   test_id : test point id (inside the subtask when files are named by subtask).
//
// Only main() and main(int argc, char **argv) are guaranteed by C++.
// main(int sub_id, int test_id) compiles with a warning but really receives
// argc and the truncated argv pointer, so the ids are read from argv here.
#include <cstdio>
#include <cstdlib>
#include <random>

static std::mt19937_64 rng;

// rand_int(l, r): a random integer in [l, r].
long long rand_int(long long l, long long r) {
	std::uniform_int_distribution<long long> dist(l, r);
	return dist(rng);
}

int main(int argc, char **argv) {
	const int sub_id = argc > 1 ? std::atoi(argv[1]) : 0;
	const int test_id = argc > 2 ? std::atoi(argv[2]) : 1;

	// Seed from sub_id / test_id: the same call always gives the same data.
	rng.seed(static_cast<std::mt19937_64::result_type>(
	    static_cast<unsigned long long>(sub_id + 2) * 1000003ULL + static_cast<unsigned long long>(test_id)));

	// TODO: replace with the real data format of this problem.
	const int n = static_cast<int>(rand_int(1, 10));
	std::printf("%d\n", n);

	for (int i = 0; i < n; i++)
		std::printf("%lld%c", rand_int(1, 1000000000LL), i + 1 == n ? '\n' : ' ');

	return 0;
}
)CPP");
}

QString DataGenerator::sourceFile(const QString &taskDirectory, const QString &baseName) {
	return findSource(generatorDirectory(taskDirectory), baseName);
}

QString DataGenerator::configPath(const QString &taskDirectory) {
	return generatorDirectory(taskDirectory) + QStringLiteral("/config.json");
}

bool DataGenerator::loadConfig(const QString &taskDirectory, Options *options) {
	QFile file(configPath(taskDirectory));

	if (! file.open(QIODevice::ReadOnly | QIODevice::Text))
		return false;

	const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();

	if (object.isEmpty())
		return false;

	if (object.contains(QStringLiteral("namePattern")))
		options->namePattern = object.value(QStringLiteral("namePattern")).toString();

	if (object.contains(QStringLiteral("inputSuffix")))
		options->inputSuffix = object.value(QStringLiteral("inputSuffix")).toString();

	if (object.contains(QStringLiteral("outputSuffix")))
		options->outputSuffix = object.value(QStringLiteral("outputSuffix")).toString();

	if (object.contains(QStringLiteral("sampleSuffix")))
		options->sampleSuffix = object.value(QStringLiteral("sampleSuffix")).toString();

	if (object.contains(QStringLiteral("subtaskMode")))
		options->subtaskMode = object.value(QStringLiteral("subtaskMode")).toBool();

	if (object.contains(QStringLiteral("subtaskNaming")))
		options->subtaskNaming = object.value(QStringLiteral("subtaskNaming")).toBool();

	if (object.contains(QStringLiteral("caseCount")))
		options->caseCount = object.value(QStringLiteral("caseCount")).toInt(options->caseCount);

	if (object.contains(QStringLiteral("sampleCount")))
		options->sampleCount = object.value(QStringLiteral("sampleCount")).toInt(options->sampleCount);

	if (object.contains(QStringLiteral("cleanData")))
		options->cleanData = object.value(QStringLiteral("cleanData")).toBool();

	if (object.contains(QStringLiteral("configureTestCases")))
		options->configureTestCases = object.value(QStringLiteral("configureTestCases")).toBool();

	if (object.contains(QStringLiteral("subtaskCases"))) {
		options->subtaskCases.clear();

		for (const QJsonValue &value : object.value(QStringLiteral("subtaskCases")).toArray())
			options->subtaskCases.append(value.toInt());
	}

	return true;
}

bool DataGenerator::saveConfig(const QString &taskDirectory, const Options &options) {
	if (taskDirectory.isEmpty())
		return false;

	if (! QDir().mkpath(generatorDirectory(taskDirectory)))
		return false;

	QJsonObject object;
	object.insert(QStringLiteral("namePattern"), options.namePattern);
	object.insert(QStringLiteral("inputSuffix"), options.inputSuffix);
	object.insert(QStringLiteral("outputSuffix"), options.outputSuffix);
	object.insert(QStringLiteral("sampleSuffix"), options.sampleSuffix);
	object.insert(QStringLiteral("subtaskMode"), options.subtaskMode);
	object.insert(QStringLiteral("subtaskNaming"), options.subtaskNaming);
	object.insert(QStringLiteral("caseCount"), options.caseCount);
	object.insert(QStringLiteral("sampleCount"), options.sampleCount);
	object.insert(QStringLiteral("cleanData"), options.cleanData);
	object.insert(QStringLiteral("configureTestCases"), options.configureTestCases);

	QJsonArray counts;

	for (int count : options.subtaskCases)
		counts.append(count);

	object.insert(QStringLiteral("subtaskCases"), counts);

	QFile file(configPath(taskDirectory));

	if (! file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;

	file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
	return true;
}

QString DataGenerator::solutionTemplate() {
	return QString::fromUtf8(R"CPP(// sol.cpp -- model solution (written by LemonLime; edit freely).
// Reads one input from stdin and writes the expected answer to stdout.
#include <cstdio>

int main() {
	// TODO: implement the solution.
	return 0;
}
)CPP");
}

bool DataGenerator::ensureSolutionTemplate(const QString &taskDirectory) {
	const QString directory = generatorDirectory(taskDirectory);

	if (directory.isEmpty() || ! QDir().mkpath(directory))
		return false;

	if (! findSource(directory, QStringLiteral("sol")).isEmpty())
		return true;

	QFile file(directory + QStringLiteral("/sol.cpp"));

	if (! file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;

	file.write(solutionTemplate().toUtf8());
	return true;
}

bool DataGenerator::ensureGeneratorTemplate(const QString &taskDirectory) {
	const QString directory = generatorDirectory(taskDirectory);

	if (directory.isEmpty() || ! QDir().mkpath(directory))
		return false;

	if (! findSource(directory, QStringLiteral("gen")).isEmpty())
		return true;

	QFile file(directory + QStringLiteral("/gen.cpp"));

	if (! file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;

	file.write(generatorTemplate().toUtf8());
	return true;
}

QList<DataGenerator::PlannedTest> DataGenerator::planTests(const Options &options, const QString &taskName,
                                                           QString *error) {
	QList<PlannedTest> tests;

	if (options.namePattern.trimmed().isEmpty()) {
		*error = tr("The file name prefix cannot be empty.");
		return tests;
	}

	const bool usesSubId = options.namePattern.contains(QStringLiteral("<subid>"));

	if (usesSubId && ! options.subtaskMode) {
		*error = tr("<subid> only works when \"Configure by Subtask\" is on.");
		return tests;
	}

	if (usesSubId && ! options.subtaskNaming) {
		*error = tr("<subid> only works when \"Name Files by Subtask\" is on.");
		return tests;
	}

	QList<int> counts;

	if (options.subtaskMode)
		counts = options.subtaskCases;
	else
		counts.append(options.caseCount);

	for (int count : counts) {
		if (count <= 0) {
			*error = tr("The number of test points must be positive.");
			return tests;
		}
	}

	if (options.sampleCount < 0) {
		*error = tr("The number of samples cannot be negative.");
		return tests;
	}

	QSet<QString> usedTests;
	QSet<QString> usedSamples;
	int globalId = 0;

	auto buildName = [&](int subtask, const QString &testId, bool useSubId) {
		QString name = options.namePattern;
		name.replace(QStringLiteral("<name>"), taskName);
		name.replace(QStringLiteral("<subid>"), useSubId ? QString::number(subtask) : QString());
		name.replace(QStringLiteral("<testid>"), testId);
		return name.trimmed();
	};

	for (int group = 0; group < counts.size(); group++) {
		for (int index = 1; index <= counts.at(group); index++) {
			globalId++;
			PlannedTest test;
			test.subtask = options.subtaskMode ? group + 1 : 0;
			test.testId = options.subtaskNaming ? index : globalId;
			test.groupIndex = options.subtaskMode ? group : globalId - 1;
			test.fileName = buildName(
			    test.subtask, options.subtaskNaming ? twoDigits(index) : QString::number(globalId), true);

			if (test.fileName.isEmpty() || test.fileName.contains(QChar('<')) ||
			    test.fileName.contains(QChar('>'))) {
				*error = tr("Unknown placeholder in the file name prefix: %1").arg(options.namePattern);
				return {};
			}

			if (usedTests.contains(test.fileName)) {
				*error = tr("The file name %1 would be used twice; add <testid> to the prefix.")
				             .arg(test.fileName);
				return {};
			}

			usedTests.insert(test.fileName);
			tests.append(test);
		}
	}

	for (int index = 1; index <= options.sampleCount; index++) {
		PlannedTest sample;
		sample.subtask = -1;
		sample.testId = index;
		sample.groupIndex = -1;
		sample.sample = true;
		sample.fileName = buildName(-1, QString::number(index), false);

		if (sample.fileName.isEmpty() || sample.fileName.contains(QChar('<')) ||
		    sample.fileName.contains(QChar('>'))) {
			*error = tr("Unknown placeholder in the file name prefix: %1").arg(options.namePattern);
			return {};
		}

		if (usedSamples.contains(sample.fileName)) {
			*error = tr("The sample file name %1 would be used twice; add <testid> to the prefix.")
			             .arg(sample.fileName);
			return {};
		}

		usedSamples.insert(sample.fileName);
		tests.append(sample);
	}

	return tests;
}

DataGenerator::DataGenerator(QObject *parent) : QObject(parent) {}

void DataGenerator::log(const QString &message) { emit logMessage(message); }

void DataGenerator::setStatus(const QString &message) { emit statusMessage(message); }

void DataGenerator::applyToTask(Task *task, const Result &result, bool subtaskMode, int timeLimit,
                                int memoryLimit) {
	if (! task || result.groups.isEmpty())
		return;

	while (! task->getTestCaseList().isEmpty())
		task->deleteTestCase(task->getTestCaseList().size() - 1);

	const int count = result.groups.size();
	const int totalScore = qMax(100, count);
	const int scorePerCase = totalScore / count;
	const int remainder = totalScore - scorePerCase * count;
	const QString separator = QDir::separator();
	const QString directory = task->getDirectoryName();

	for (int index = 0; index < count; index++) {
		auto *testCase = new TestCase;
		testCase->setFullScore(scorePerCase + (index < remainder ? 1 : 0));
		testCase->setTimeLimit(timeLimit);
		testCase->setMemoryLimit(memoryLimit);

		const Group &group = result.groups.at(index);

		for (int file = 0; file < group.inputs.size() && file < group.outputs.size(); file++)
			testCase->addSingleCase(directory + separator + QStringLiteral("data") + separator +
			                            group.inputs.at(file),
			                        directory + separator + QStringLiteral("data") + separator +
			                            group.outputs.at(file));

		if (subtaskMode && index > 0) {
			QList<int> depends;
			depends.append(index);
			testCase->setDependenceSubtask(depends);
		}

		task->addTestCase(testCase);
	}
}

bool DataGenerator::run(const Options &options, Result *result) {
	*result = Result();
	const QString taskDir = QDir::fromNativeSeparators(options.taskDirectory);

	if (taskDir.isEmpty()) {
		result->error = tr("No task selected.");
		return false;
	}

	const QString generatorDir = generatorDirectory(taskDir);

	if (! QDir(generatorDir).exists()) {
		ensureGeneratorTemplate(taskDir);
	}

	if (! QDir(generatorDir).exists()) {
		result->error = tr("The generator folder does not exist: %1")
		                    .arg(QDir::toNativeSeparators(generatorDir));
		return false;
	}

	const QString genSource = findSource(generatorDir, QStringLiteral("gen"));
	const QString solSource = findSource(generatorDir, QStringLiteral("sol"));

	if (genSource.isEmpty() || solSource.isEmpty()) {
		result->error = tr("Put gen.cpp (generator) and sol.cpp (model solution) into %1.")
		                    .arg(QDir::toNativeSeparators(generatorDir));
		return false;
	}

	{
		QFile solFile(solSource);

		if (solFile.open(QIODevice::ReadOnly | QIODevice::Text) &&
		    QString::fromUtf8(solFile.readAll()) == solutionTemplate()) {
			result->error = tr("%1 is still the template. Write the model solution first.")
			                    .arg(QFileInfo(solSource).fileName());
			return false;
		}
	}

	const QString taskName = QFileInfo(taskDir).fileName();
	const QList<PlannedTest> tests = planTests(options, taskName, &result->error);

	if (tests.isEmpty()) {
		if (result->error.isEmpty())
			result->error = tr("Nothing to generate.");

		return false;
	}

	const QString sandboxDir = sandboxDirectory(taskDir);

	if (! QDir().mkpath(sandboxDir)) {
		result->error = tr("Cannot create %1.").arg(QDir::toNativeSeparators(sandboxDir));
		return false;
	}

	clearFiles(sandboxDir);

	for (const QString &name : {QStringLiteral("gen"), QStringLiteral("sol")}) {
		const QString stale = generatorDir + QChar('/') + name + executableSuffix();

		if (QFileInfo::exists(stale))
			QFile::remove(stale);
	}

	if (! compile(options, genSource, QStringLiteral("gen"), QStringLiteral("sol"), sandboxDir, &result->error) ||
	    ! compile(options, solSource, QStringLiteral("sol"), QStringLiteral("gen"), sandboxDir, &result->error))
		return false;

	const QString dataDir = taskDir + QStringLiteral("/data");
	const QString downDir = taskDir + QStringLiteral("/down");
	QDir().mkpath(dataDir);
	QDir().mkpath(downDir);

	if (options.cleanData) {
		const int removed = clearFiles(dataDir) + clearFiles(downDir);

		if (removed > 0)
			log(tr("Removed %1 old file(s) from data/ and down/.").arg(removed));
	}

	const QString genExecutable = QDir(sandboxDir).absoluteFilePath(QStringLiteral("gen") + executableSuffix());
	const QString solExecutable = QDir(sandboxDir).absoluteFilePath(QStringLiteral("sol") + executableSuffix());
	const QProcessEnvironment environment = mergedEnvironment(options.environment);

	QList<Group> groups;
	const int groupTotal = options.subtaskMode ? options.subtaskCases.size() : options.caseCount;

	for (int count = 0; count < groupTotal; count++)
		groups.append(Group());

	for (int index = 0; index < tests.size(); index++) {
		const PlannedTest &test = tests.at(index);
		const QString directory = test.sample ? downDir : dataDir;
		const QString inputName = test.fileName + options.inputSuffix;
		const QString outputName =
		    test.fileName + (test.sample ? options.sampleSuffix : options.outputSuffix);
		const QString progress = tr("%1 / %2").arg(index + 1).arg(tests.size());

		if (test.sample) {
			log(tr("Generating sample %1 (test %2) ...").arg(inputName).arg(test.testId));
			setStatus(tr("Generating sample %1 (%2) ...").arg(inputName, progress));
		} else {
			log(tr("Generating %1 (subtask %2, test %3) ...")
			        .arg(inputName)
			        .arg(test.subtask)
			        .arg(test.testId));
			setStatus(tr("Generating %1 (%2) ...").arg(inputName, progress));
		}

		const ProcessOutcome generator = runProcess(
		    genExecutable, {QString::number(test.subtask), QString::number(test.testId)}, sandboxDir,
		    environment, options.runTimeLimit, {}, {});

		if (! generator.started) {
			result->error = tr("Cannot start the generator: %1").arg(firstLines(generator.errorOutput));
			return false;
		}

		if (! firstLines(generator.errorOutput).isEmpty())
			log(firstLines(generator.errorOutput));

		if (! generator.finished) {
			result->error = tr("The generator did not finish in %1 s on %2.")
			                    .arg(options.runTimeLimit / 1000)
			                    .arg(inputName);
			return false;
		}

		if (generator.exitCode != 0) {
			result->error =
			    tr("The generator exited with code %1 on %2.").arg(generator.exitCode).arg(inputName);
			return false;
		}

		if (generator.output.isEmpty()) {
			result->error = tr("The generator wrote nothing to stdout on %1.").arg(inputName);
			return false;
		}

		const QString inputPath = directory + QChar('/') + inputName;
		QFile inputFile(inputPath);

		if (! inputFile.open(QIODevice::WriteOnly) ||
		    inputFile.write(generator.output) != static_cast<qint64>(generator.output.size())) {
			result->error = tr("Cannot write %1.").arg(QDir::toNativeSeparators(inputPath));
			return false;
		}

		inputFile.close();

		setStatus(tr("Running the model solution on %1 (%2) ...").arg(inputName, progress));

		QElapsedTimer timer;
		timer.start();

		const ProcessOutcome solution =
		    runProcess(solExecutable, {}, sandboxDir, environment, options.runTimeLimit, inputPath,
		               directory + QChar('/') + outputName);

		if (! solution.started || ! solution.finished || solution.exitCode != 0) {
			result->error = tr("The model solution failed on %1: %2")
			                    .arg(inputName, ! firstLines(solution.errorOutput).isEmpty()
			                                        ? firstLines(solution.errorOutput)
			                                        : tr("exit code %1").arg(solution.exitCode));
			return false;
		}

		log(tr("Answer written: %1 (%2 ms)").arg(outputName).arg(timer.elapsed()));
		setStatus(tr("Answer written: %1 (%2)").arg(outputName, progress));

		if (test.sample) {
			result->sampleAnswers.append(outputName);
			continue;
		}

		groups[test.groupIndex].inputs.append(inputName);
		groups[test.groupIndex].outputs.append(outputName);
	}

	for (const Group &group : std::as_const(groups))
		if (! group.inputs.isEmpty())
			result->groups.append(group);

	if (result->groups.isEmpty()) {
		result->error = tr("No test case was generated.");
		return false;
	}

	log(tr("Generated %1 test case(s) and %2 sample(s).")
	        .arg(result->groups.size())
	        .arg(result->sampleAnswers.size()));
	setStatus(tr("Generated %1 test case(s) and %2 sample(s).")
	              .arg(result->groups.size())
	              .arg(result->sampleAnswers.size()));
	result->ok = true;
	return true;
}

bool DataGenerator::compile(const Options &options, const QString &sourceFile, const QString &baseName,
                            const QString &otherBaseName, const QString &sandbox, QString *error) {
	const QFileInfo sourceInfo(sourceFile);
	const QString directory = sourceInfo.absolutePath();
	const QString built = directory + QChar('/') + baseName + executableSuffix();
	const QString executable = sandbox + QChar('/') + baseName + executableSuffix();

	if (options.compilerLocation.isEmpty()) {
		*error = tr("No C++ compiler is configured. Add one in Settings → Compilers first.");
		return false;
	}

	QString extra;

	for (const QString &name : extraSources(directory, baseName, otherBaseName))
		extra += QChar(' ') + name;

	QStringList arguments = splitArguments(options.compilerArguments);

	for (QString &argument : arguments) {
		argument.replace(QStringLiteral("%s.*"), sourceInfo.fileName() + extra);
		argument.replace(QStringLiteral("%s"), baseName);
	}

	log(tr("Compiling %1 ...").arg(sourceInfo.fileName()));
	setStatus(tr("Compiling %1 ...").arg(sourceInfo.fileName()));
	const ProcessOutcome outcome = runProcess(options.compilerLocation, arguments, directory,
	                                          mergedEnvironment(options.environment), options.compileTimeLimit,
	                                          {}, {});

	if (! outcome.started) {
		*error = tr("Cannot start the compiler: %1").arg(firstLines(outcome.errorOutput));
		return false;
	}

	if (! outcome.finished) {
		*error = tr("Compiling %1 took longer than %2 s.")
		             .arg(sourceInfo.fileName())
		             .arg(options.compileTimeLimit / 1000);
		return false;
	}

	if (outcome.exitCode != 0 || ! QFileInfo::exists(built)) {
		const QString output =
		    ! firstLines(outcome.errorOutput).isEmpty() ? firstLines(outcome.errorOutput) : firstLines(outcome.output);
		*error = tr("Compiling %1 failed: %2")
		             .arg(sourceInfo.fileName(),
		                  ! output.isEmpty() ? output : tr("exit code %1").arg(outcome.exitCode));
		return false;
	}

	QFile::remove(executable);

	if (! QFile::rename(built, executable)) {
		if (! QFile::copy(built, executable)) {
			*error = tr("Cannot move %1 into %2.")
			             .arg(QDir::toNativeSeparators(built), QDir::toNativeSeparators(sandbox));
			return false;
		}

		QFile::remove(built);
	}

	return true;
}
