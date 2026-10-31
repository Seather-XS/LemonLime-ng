/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QList>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

class Task;

class DataGenerator : public QObject {
	Q_OBJECT

  public:
	enum Stage { InputStage = 0, OutputStage = 1 };

	struct Options {
		enum class WriteMode { None, Overwrite, Append };

		QString taskDirectory;
		QString namePattern;
		QString sampleNamePattern;
		QString inputSuffix{QStringLiteral(".in")};
		QString outputSuffix{QStringLiteral(".ans")};
		QString sampleSuffix{QStringLiteral(".ans")};
		bool subtaskMode{false};
		int caseCount{10};
		QList<int> subtaskCases;
		bool subtaskNaming{false};
		int sampleCount{0};
		WriteMode writeMode{WriteMode::Overwrite};
		QList<int> onlyTests; // planned test indices to (re)generate; empty means all
		QString compilerLocation;
		QString compilerArguments;
		QProcessEnvironment environment;
		int compileTimeLimit{10000};
		int runTimeLimit{60000};
	};

	struct Group {
		QStringList inputs;
		QStringList outputs;
	};

	struct PlannedTest {
		int subtask{0};
		int testId{0};
		int groupIndex{-1};
		bool sample{false};
		QString fileName;
	};

	struct Result {
		bool ok{false};
		QString error;
		QList<Group> groups;
		QStringList sampleAnswers;

		int testCount() const;
	};

	struct FileStatus {
		QString path;
		int state{0};
		QStringList details;
	};

	explicit DataGenerator(QObject *parent = nullptr);

	bool run(const Options &options, Result *result);

	static QString generatorDirectory(const QString &taskDirectory);

	static QString generatorTemplate();
	static QString solutionTemplate();
	static bool ensureGeneratorTemplate(const QString &taskDirectory);
	static bool ensureSolutionTemplate(const QString &taskDirectory);
	static QString sourceFile(const QString &taskDirectory, const QString &baseName);

	static QString configPath(const QString &taskDirectory);
	static bool loadConfig(const QString &taskDirectory, Options *options);
	static bool saveConfig(const QString &taskDirectory, const Options &options);

	static QList<FileStatus> loadStatuses(const QString &taskDirectory);
	static bool saveStatuses(const QString &taskDirectory, const QList<FileStatus> &statuses);

	static QList<PlannedTest> planTests(const Options &options, const QString &taskName, QString *error);

	static void applyToTask(Task *task, const Result &result, bool subtaskMode, int timeLimit, int memoryLimit,
	                        bool append);

  signals:
	void logMessage(const QString &message);
	void statusMessage(const QString &message);
	void fileStarted(int testIndex, int stage);
	void fileFinished(int testIndex, int stage, bool ok, const QString &detail);

  private:
	void log(const QString &message);
	void setStatus(const QString &message);
	bool compile(const Options &options, const QString &sourceFile, const QString &baseName,
	             const QString &excludeBase, const QString &sandbox, QString *error);
};
