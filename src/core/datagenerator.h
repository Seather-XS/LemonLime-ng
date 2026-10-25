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
	struct Options {
		QString taskDirectory;
		QString namePattern;
		QString inputSuffix{QStringLiteral(".in")};
		QString outputSuffix{QStringLiteral(".ans")};
		QString sampleSuffix{QStringLiteral(".ans")};
		bool subtaskMode{false};
		int caseCount{10};
		QList<int> subtaskCases;
		bool subtaskNaming{false};
		int sampleCount{0};
		bool cleanData{true};
		bool configureTestCases{true};
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
	};

	explicit DataGenerator(QObject *parent = nullptr);

	bool run(const Options &options, Result *result);

	static QString generatorDirectory(const QString &taskDirectory);
	static QString sandboxDirectory(const QString &taskDirectory);

	static QString generatorTemplate();
	static QString solutionTemplate();
	static bool ensureGeneratorTemplate(const QString &taskDirectory);
	static bool ensureSolutionTemplate(const QString &taskDirectory);
	static QString sourceFile(const QString &taskDirectory, const QString &baseName);

	static QString configPath(const QString &taskDirectory);
	static bool loadConfig(const QString &taskDirectory, Options *options);
	static bool saveConfig(const QString &taskDirectory, const Options &options);

	static QList<PlannedTest> planTests(const Options &options, const QString &taskName, QString *error);

	static void applyToTask(Task *task, const Result &result, bool subtaskMode, int timeLimit, int memoryLimit);

  signals:
	void logMessage(const QString &message);
	void statusMessage(const QString &message);

  private:
	void log(const QString &message);
	void setStatus(const QString &message);
	bool compile(const Options &options, const QString &sourceFile, const QString &baseName,
	             const QString &excludeBase, const QString &sandbox, QString *error);
};
