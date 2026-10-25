/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lemon.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "base/LemonBase.hpp"
#include "base/LemonBaseApplication.hpp"
#include "base/LemonLog.hpp"
#include "base/LemonUtils.hpp"
#include "base/settings.h"
#include "base/violation.h"
#include "component/exportutil/exportutil.h"
#include "core/contest.h"
#include "core/dayproject.h"
#include "core/packagebuilder.h"
#include "core/processrunner.h"
#include "core/statementbuilder.h"
#include "core/taskjudger.h"
#include "exportwidget.h"
#include "pdfpreview.h"
#include "resultviewer.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "statementeditwidget.h"
#include "statisticsbrowser.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplashScreen>
#include <QStandardPaths>
#include <QListWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <chrono>
#include <utility>

#define LEMON_MODULE_NAME "Main"

namespace {
	void readProjectContext(const QString &dayFile, QString &projectTitle, QString &dayTitle) {
		const QFileInfo dayInfo(dayFile);
		const QString base = dayInfo.completeBaseName();
		QStringList roots;
		roots << dayInfo.absolutePath() << QFileInfo(dayInfo.absolutePath()).absolutePath();

		for (const QString &root : roots) {
			QFile file(QDir(root).absoluteFilePath(QStringLiteral("contest.conf")));

			if (! file.open(QIODevice::ReadOnly))
				continue;

			const DayProject project =
			    DayProject::fromJson(QJsonDocument::fromJson(file.readAll()).object());

			if (project.title.isEmpty() && project.days.isEmpty())
				continue;

			projectTitle = project.title;

			for (const DayEntry &entry : project.days) {
				if (QFileInfo(entry.file).completeBaseName() == base) {
					dayTitle = entry.title;
					break;
				}
			}

			return;
		}
	}

	QString resolvedStatementFileName(const QString &dayFile, Contest &contest) {
		QString projectTitle;
		QString dayTitle;
		readProjectContext(dayFile, projectTitle, dayTitle);
		const QString contestTitle = projectTitle.isEmpty() ? contest.getContestTitle() : projectTitle;
		return Lemon::common::ResolveStatementPdfName(contest.getStatementPdfName(),
		                                              QFileInfo(dayFile).completeBaseName(), dayTitle,
		                                              contestTitle) +
		       QStringLiteral(".pdf");
	}
} 

void initLogger() {
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	console_sink->set_level(spdlog::level::warn);
	QDir logDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QDir::separator() +
	            "logs");
	logDir.mkpath(".");
	auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
	    (logDir.path() + QDir::separator() + "lemonlime-log.txt").toStdString(), 0, 0, false, 30);
	file_sink->set_level(spdlog::level::trace);
	Lemon::base::logger =
	    std::make_shared<spdlog::logger>(spdlog::logger("lemonlime", {console_sink, file_sink}));
	Lemon::base::logger->flush_on(spdlog::level::trace);
	spdlog::flush_every(std::chrono::seconds(5));
}

int main(int argc, char *argv[]) {

	QCoreApplication::setApplicationName("LemonLime-ng");

	initLogger();

	if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--build-statement")) {
		QCoreApplication app(argc, argv);
		StatementBuilder builder;
		builder.setSourceFile(QString::fromLocal8Bit(argv[2]));
		builder.setOutputBase(QString::fromLocal8Bit(argv[3]));
		builder.setTemplate(QString::fromLocal8Bit(argv[4]));
		QFile logFile(QString::fromLocal8Bit(argv[3]) + QStringLiteral(".buildlog.txt"));
		logFile.open(QIODevice::WriteOnly | QIODevice::Text);
		QTextStream logStream(&logFile);
		QObject::connect(&builder, &StatementBuilder::logMessage,
		                 [&logStream](const QString &line) { logStream << line << '\n'; logStream.flush(); });
		const bool ok = builder.build();

		if (! ok)
			logStream << QStringLiteral("ERROR: ") << builder.lastError() << '\n';

		logStream.flush();
		return ok ? 0 : 1;
	}

	if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--export-score")) {
		QApplication app(argc, argv);
		const QString dayFile = QString::fromLocal8Bit(argv[2]);
		const QString outBase = QString::fromLocal8Bit(argv[3]);

		QFile day(dayFile);

		if (! day.open(QIODevice::ReadOnly)) {
			LOG("export-score: cannot open", dayFile);
			return 2;
		}

		Settings settings;
		settings.loadSettings();
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("export-score: broken contest file", dayFile);
			return 3;
		}

		QStringList report;
		report << QStringLiteral("regions enabled: %1").arg(contest.getRegionEnabled() ? 1 : 0);
		{
			ResultViewer viewer;
			viewer.setContest(&contest);
			viewer.refreshViewer();
			QStringList header;

			for (int column = 0; column < viewer.columnCount(); column++)
				header << viewer.horizontalHeaderItem(column)->text();

			report << QStringLiteral("columns(%1): ").arg(viewer.columnCount()) + header.join(QStringLiteral(" | "));

			for (int row = 0; row < qMin(3, viewer.rowCount()); row++) {
				QStringList cells;

				for (int column = 0; column < qMin(5, viewer.columnCount()); column++)
					cells << (viewer.item(row, column) ? viewer.item(row, column)->text() : QString());

				report << QStringLiteral("row %1: ").arg(row) + cells.join(QStringLiteral(" | "));
			}
		}

		ExportUtil::exportHtml(nullptr, &contest, outBase + QStringLiteral(".html"));
		ExportUtil::exportCsv(nullptr, &contest, outBase + QStringLiteral(".csv"));

		QFile out(outBase + QStringLiteral(".columns.txt"));

		if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&out);
			stream << report.join(QChar('\n')) << '\n';
		}

		return 0;
	}

	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--export-reports")) {
		QApplication app(argc, argv);
		const QString dayFile = QString::fromLocal8Bit(argv[2]);
		QFile day(dayFile);

		if (! day.open(QIODevice::ReadOnly)) {
			LOG("export-reports: cannot open", dayFile);
			return 2;
		}

		Settings settings;
		settings.loadSettings();
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("export-reports: broken contest file", dayFile);
			return 3;
		}

		QDir::setCurrent(QFileInfo(dayFile).absolutePath());
		ExportUtil::exportResult(nullptr, &contest);
		StatisticsBrowser::exportStatistics(nullptr, &contest);
		return 0;
	}

	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--judge-day")) {
		QApplication app(argc, argv);
		const QString dayFile = QString::fromLocal8Bit(argv[2]);
		QFile day(dayFile);

		if (! day.open(QIODevice::ReadOnly)) {
			LOG("judge-day: cannot open", dayFile);
			return 2;
		}

		QString onlyContestant;
		QString onlyTask;
		bool ignoreRules = true;

		for (int i = 3; i < argc; i++) {
			const QString option = QString::fromLocal8Bit(argv[i]);

			if (option == QLatin1String("--rules"))
				ignoreRules = false;
			else if (option == QLatin1String("--contestant") && i + 1 < argc)
				onlyContestant = QString::fromLocal8Bit(argv[++i]);
			else if (option == QLatin1String("--task") && i + 1 < argc)
				onlyTask = QString::fromLocal8Bit(argv[++i]);
		}

		Settings settings;
		settings.loadSettings();
		QDir::setCurrent(QFileInfo(dayFile).absolutePath());
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("judge-day: broken contest file", dayFile);
			return 3;
		}

		QList<std::pair<QString, QVector<int>>> lists;

		for (auto *contestant : contest.getContestantList()) {
			if (! onlyContestant.isEmpty() && contestant->getContestantName() != onlyContestant)
				continue;

			QVector<int> tasks;

			for (int i = 0; i < contest.getTaskList().size(); i++) {
				if (! onlyTask.isEmpty() && contest.getTask(i)->getProblemTitle() != onlyTask)
					continue;

				tasks.append(i);
			}

			if (! tasks.isEmpty())
				lists.append({contestant->getContestantName(), tasks});
		}

		LOG("judge-day: contestants", lists.size());
		QObject::connect(&contest, &Contest::contestantSkipped,
		                 [](const QString &name, const QString &, const QString &, int progress) {
			                 LOG("judge-day: skipped", name, "progress", progress);
		                 });
		contest.judge(lists, ignoreRules);

		for (auto *contestant : contest.getContestantList()) {
			for (int i = 0; i < contest.getTaskList().size(); i++) {
				if (! onlyTask.isEmpty() && contest.getTask(i)->getProblemTitle() != onlyTask)
					continue;

				const CompileState state = contestant->getCompileState(i);
				QStringList verdicts;

				for (const QList<ResultState> &perTest : contestant->getResult(i))
					for (ResultState verdict : perTest) {
						QString text;
						QString fr;
						QString bg;
						Settings::setTextAndColor(verdict, text, fr, bg);
						verdicts << text;
					}

				LOG("judge-day: result", contestant->getContestantName(),
				    contest.getTask(i)->getProblemTitle(), "compile=", static_cast<int>(state),
				    "score=", contestant->getTaskScore(i), "verdicts=", verdicts.join(QChar(',')),
				    "message=", contestant->getCompileMessage(i));
			}
		}

		LOG("judge-day: done");
		return 0;
	}

	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--check-live-skip")) {
		QApplication app(argc, argv);
		const QString dayFile = QString::fromLocal8Bit(argv[2]);
		QFile day(dayFile);

		if (! day.open(QIODevice::ReadOnly)) {
			LOG("check-live-skip: cannot open", dayFile);
			return 2;
		}

		Settings settings;
		settings.loadSettings();
		QDir::setCurrent(QFileInfo(dayFile).absolutePath());
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("check-live-skip: broken contest file", dayFile);
			return 3;
		}

		ResultViewer viewer;
		viewer.setContest(&contest);

		QHash<QString, int> rowOf;

		for (int i = 0; i < contest.getContestantList().size(); i++)
			rowOf[contest.getContestantList()[i]->getContestantName()] = i;

		for (auto *contestant : contest.getContestantList())
			contestant->setDisqualifyState(Contestant::NotDisqualified, QString());

		viewer.refreshViewer();

		const int scoreColumn = 2 + (contest.getRegionEnabled() ? 1 : 0);
		const auto isDisqualifiedColor = [](const QColor &color) {
			return color == QColor(200, 60, 60) || color == QColor(160, 90, 200);
		};
		const auto cellColor = [](QTableWidget *table, int row, int column) {
			return table->item(row, column) ? table->item(row, column)->background().color() : QColor();
		};

		QStringList report;
		int stale = 0;
		int skipped = 0;

		for (auto *contestant : contest.getContestantList()) {
			const int row = rowOf.value(contestant->getContestantName());
			const QColor color = cellColor(&viewer, row, scoreColumn);
			report << QStringLiteral("before: %1 row=%2 color=%3%4")
			              .arg(contestant->getContestantName())
			              .arg(row)
			              .arg(color.name())
			              .arg(isDisqualifiedColor(color) ? QStringLiteral(" (已经是取消色，测试前提不成立)")
			                                              : QString());
		}

		QObject::connect(&contest, &Contest::contestantSkipped,
		                 [&](const QString &name, const QString &, const QString &, int) {
			                 const int row = rowOf.value(name, -1);
			                 const QColor color = row >= 0 ? cellColor(&viewer, row, scoreColumn) : QColor();
			                 const QString tip = row >= 0 && viewer.item(row, scoreColumn)
			                                         ? viewer.item(row, scoreColumn)->toolTip()
			                                         : QString();
			                 const bool ok = row >= 0 && isDisqualifiedColor(color) && ! tip.isEmpty();
			                 skipped++;

			                 if (! ok)
				                 stale++;

			                 report << QStringLiteral("at signal: %1 row=%2 color=%3 tooltip=%4 %5")
			                               .arg(name)
			                               .arg(row)
			                               .arg(color.name())
			                               .arg(tip.isEmpty() ? QStringLiteral("<空>") : tip)
			                               .arg(ok ? QStringLiteral("OK") : QStringLiteral("STALE"));
		                 });

		QList<std::pair<QString, QVector<int>>> lists;

		for (auto *contestant : contest.getContestantList()) {
			QVector<int> tasks;

			for (int i = 0; i < contest.getTaskList().size(); i++)
				tasks.append(i);

			if (! tasks.isEmpty())
				lists.append({contestant->getContestantName(), tasks});
		}

		contest.judge(lists, false);

		report.prepend(QStringLiteral("skipped=%1 stale=%2").arg(skipped).arg(stale));

		QFile out(QStringLiteral("live-skip-check.txt"));

		if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&out);
			stream << report.join(QChar('\n')) << '\n';
		}

		LOG("check-live-skip: skipped", skipped, "stale", stale);

		if (skipped == 0 || stale != 0)
			return 1;

		return 0;
	}

	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--export-package")) {
		QApplication app(argc, argv);
		const QString dayFile = QString::fromLocal8Bit(argv[2]);
		QFile day(dayFile);

		if (! day.open(QIODevice::ReadOnly)) {
			LOG("export-package: cannot open", dayFile);
			return 2;
		}

		bool wrap = false;
		bool nested = false;
		bool perTask = true;
		bool structure = true;
		bool samples = false;
		bool statementGiven = false;
		QStringList statements;
		QString password;
		PackageBuilder::Kind kind = PackageBuilder::ContestantPackage;

		for (int i = 3; i < argc; i++) {
			const QString option = QString::fromLocal8Bit(argv[i]);

			if (option == QLatin1String("--wrap"))
				wrap = true;
			else if (option == QLatin1String("--nested"))
				nested = true;
			else if (option == QLatin1String("--no-per-task"))
				perTask = false;
			else if (option == QLatin1String("--no-structure"))
				structure = false;
			else if (option == QLatin1String("--samples"))
				samples = true;
			else if (option == QLatin1String("--kind") && i + 1 < argc) {
				const QString value = QString::fromLocal8Bit(argv[++i]);

				if (value == QLatin1String("testdata"))
					kind = PackageBuilder::TestDataPackage;
				else if (value == QLatin1String("answers"))
					kind = PackageBuilder::AnswersPackage;
				else
					kind = PackageBuilder::ContestantPackage;
			} else if (option == QLatin1String("--password") && i + 1 < argc)
				password = QString::fromLocal8Bit(argv[++i]);
			else if (option == QLatin1String("--statement") && i + 1 < argc) {
				statementGiven = true;
				statements << QString::fromLocal8Bit(argv[++i]);
			} else if (option == QLatin1String("--no-statement"))
				statementGiven = true;
		}

		Settings settings;
		settings.loadSettings();
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("export-package: broken contest file", dayFile);
			return 3;
		}

		const QString dayDirectory = QFileInfo(dayFile).absolutePath();
		PackageBuilder builder;
		builder.setKind(kind);
		builder.setDayFile(dayFile);
		builder.setContest(&contest);
		builder.setWrapInFolder(wrap);
		builder.setNestedZip(nested);
		builder.setOneFolderPerTask(perTask);
		builder.setKeepStructure(structure);
		builder.setIncludeSamples(samples);
		builder.setPassword(password);
		builder.setEncryptionMethod(PackageBuilder::Aes256Encryption);
		builder.setStatementFiles(statementGiven ? statements
		                                         : QStringList{resolvedStatementFileName(dayFile, contest)});
		builder.setOutputFile(builder.defaultOutputFile());
		QFile logFile(QDir(dayDirectory).absoluteFilePath(QStringLiteral("export-log.txt")));
		logFile.open(QIODevice::WriteOnly | QIODevice::Text);
		QTextStream logStream(&logFile);
		QObject::connect(&builder, &PackageBuilder::logMessage,
		                 [&logStream](const QString &line) { logStream << line << '\n'; logStream.flush(); });

		if (kind == PackageBuilder::TestDataPackage)
			logStream << "duplicate task files: " << (builder.hasDuplicateTaskFiles() ? "yes" : "no") << '\n';

		if (! builder.build()) {
			logStream << QStringLiteral("ERROR: ") << builder.lastError() << '\n';
			return 1;
		}

		return 0;
	}

	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--pdf-name")) {
		QApplication app(argc, argv);
		const QString dayFile = QString::fromLocal8Bit(argv[2]);
		QFile day(dayFile);

		if (! day.open(QIODevice::ReadOnly)) {
			LOG("pdf-name: cannot open", dayFile);
			return 2;
		}

		Settings settings;
		settings.loadSettings();
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("pdf-name: broken contest file", dayFile);
			return 3;
		}

		QString projectTitle;
		QString dayTitle;
		readProjectContext(dayFile, projectTitle, dayTitle);

		QFile out(QDir(QFileInfo(dayFile).absolutePath()).absoluteFilePath(QStringLiteral("statement-name.txt")));

		if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&out);
			stream << "day=" << QFileInfo(dayFile).completeBaseName() << '\n';
			stream << "title-day=" << dayTitle << '\n';
			stream << "title=" << (projectTitle.isEmpty() ? contest.getContestTitle() : projectTitle) << '\n';
			stream << "pattern=" << contest.getStatementPdfName() << '\n';
			stream << "file=" << resolvedStatementFileName(dayFile, contest) << '\n';
		}

		return 0;
	}

	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--check-ccf")) {
		QApplication app(argc, argv);
		QStringList report;
		int problems = 0;
		const auto fail = [&](const QString &line) {
			problems++;
			report << (QStringLiteral("FAIL ") + line);
		};

		{
			Settings settings;
			report << QStringLiteral("require return zero (default): %1")
			              .arg(settings.getRequireReturnZero() ? 1 : 0);

			if (! settings.getRequireReturnZero())
				fail(QStringLiteral("the CCF rule 'main must return 0' is off by default"));
		}

		{
			const QStringList gpp{QStringLiteral("cpp"), QStringLiteral("cc"), QStringLiteral("cxx")};
			const QStringList files{QStringLiteral("cake.cxx"), QStringLiteral("cake.cc"),
			                        QStringLiteral("cake.cpp")};
			const QStringList ordered = TaskJudger::orderSourceFiles(gpp, files);
			report << QStringLiteral("g++ suffixes %1 -> picked %2 (order: %3)")
			              .arg(gpp.join(QStringLiteral(";")), ordered.value(0),
			                   ordered.join(QStringLiteral(", ")));

			if (ordered.value(0) != QStringLiteral("cake.cpp") ||
			    ordered != QStringList{QStringLiteral("cake.cpp"), QStringLiteral("cake.cc"),
			                           QStringLiteral("cake.cxx")})
				fail(QStringLiteral("the compiler's own suffix order is not respected"));

			const QStringList cOnly{QStringLiteral("c")};
			QString picked;
			QString pickedBy;
			const QList<QPair<QString, QStringList>> compilers{
			    {QStringLiteral("gcc"), cOnly}, {QStringLiteral("g++"), gpp},
			    {QStringLiteral("fpc"), {QStringLiteral("pas"), QStringLiteral("pp")}}};

			for (const auto &compiler : compilers) {
				const QString candidate =
				    TaskJudger::orderSourceFiles(compiler.second, files + QStringList{QStringLiteral("cake.c"),
				                                                                      QStringLiteral("cake.pas")})
				        .value(0);

				if (compiler.second.contains(QFileInfo(candidate).suffix().toLower())) {
					picked = candidate;
					pickedBy = compiler.first;
					break;
				}
			}

			report << QStringLiteral("first compiler that can take a source file: %1 -> %2")
			              .arg(pickedBy, picked);

			if (picked != QStringLiteral("cake.c"))
				fail(QStringLiteral("the compiler list order does not give .c priority over .cpp"));

			if (TaskJudger::suffixRank(gpp, QStringLiteral("cake.py")) != gpp.size())
				fail(QStringLiteral("an unknown suffix must rank last"));
		}

		{
			const QVector<ViolationRule> rules = Violation::defaultRules();
			QStringList names;

			for (const ViolationRule &rule : rules)
				names << rule.text;

			report << QStringLiteral("default violation rules: %1").arg(names.join(QStringLiteral(", ")));

			for (const QString &needed : {QStringLiteral("system"), QStringLiteral("popen"),
			                              QStringLiteral("fork"), QStringLiteral("exec"),
			                              QStringLiteral("CreateProcess"),
			                              QStringLiteral("CreateThread"),
			                              QStringLiteral("pthread_create"), QStringLiteral("socket"),
			                              QStringLiteral("WSAStartup"), QStringLiteral("#pragma"),
			                              QStringLiteral("__asm__")}) {
				if (! names.contains(needed)) {
					fail(QStringLiteral("the default rules do not cover ") + needed);
					continue;
				}
			}

			const QString code = QStringLiteral("int main() {\n"
			                                    "  system(\"pause\");\n"
			                                    "  pthread_create(0, 0, 0, 0);\n"
			                                    "  socket(1, 2, 3);\n"
			                                    "  return 0;\n"
			                                    "}\n");
			const QVector<Violation::Match> hits = Violation::checkSource(code, rules);
			QStringList hitNames;

			for (const Violation::Match &hit : hits)
				hitNames << hit.rule.text;

			report << QStringLiteral("violations in a sample: %1").arg(hitNames.join(QStringLiteral(", ")));

			if (! hitNames.contains(QStringLiteral("system")) ||
			    ! hitNames.contains(QStringLiteral("pthread_create")) ||
			    ! hitNames.contains(QStringLiteral("socket")))
				fail(QStringLiteral("the default rules did not catch the CCF forbidden calls"));

			const QString clean = QStringLiteral("// system(\n"
			                                     "int connect(int a, int b) { return a + b; }\n"
			                                     "int main() { return connect(1, 2) - 3; }\n");
			const QVector<Violation::Match> cleanHits = Violation::checkSource(clean, rules);
			report << QStringLiteral("violations in clean code: %1").arg(cleanHits.size());

			if (! cleanHits.isEmpty())
				fail(QStringLiteral("the default rules flag innocent code"));

			{
				const auto languageOf = [](const QString &name, const QStringList &extensions) {
					return Violation::languageNameFor(name, extensions);
				};
				const auto rulesOf = [](const QString &name, const QStringList &extensions) {
					QStringList texts;

					for (const ViolationRule &rule : Violation::defaultRulesFor(name, extensions))
						texts << rule.text;

					return texts;
				};

				report << QStringLiteral("languages: %1")
				              .arg(languageOf(QStringLiteral("g++"), {QStringLiteral("cpp")}) + QStringLiteral(", ") +
				                   languageOf(QStringLiteral("fpc"), {QStringLiteral("pas")}) + QStringLiteral(", ") +
				                   languageOf(QStringLiteral("fbc"), {QStringLiteral("bas")}) + QStringLiteral(", ") +
				                   languageOf(QStringLiteral("jdk"), {QStringLiteral("java")}) + QStringLiteral(", ") +
				                   languageOf(QStringLiteral("python"), {QStringLiteral("py")}));

				if (languageOf(QStringLiteral("g++"), {}) != QStringLiteral("C/C++"))
					fail(QStringLiteral("g++ is not mapped to the C/C++ command list"));

				if (languageOf(QString(), {QStringLiteral("pas")}) != QStringLiteral("Free Pascal"))
					fail(QStringLiteral("the .pas extension is not mapped to the Pascal command list"));

				if (languageOf(QStringLiteral("my-own-cc"), {QStringLiteral("cpp")}) != QStringLiteral("C/C++"))
					fail(QStringLiteral("a custom compiler with .cpp is not mapped to C/C++"));

				const QStringList pascal = rulesOf(QStringLiteral("fpc"), {QStringLiteral("pas")});

				if (! pascal.contains(QStringLiteral("ExecuteProcess")) ||
				    ! pascal.contains(QStringLiteral("fpSystem")))
					fail(QStringLiteral("the Pascal command list has no process-spawning entries"));

				const QStringList java = rulesOf(QStringLiteral("jdk"), {QStringLiteral("java")});

				if (! java.contains(QStringLiteral("ProcessBuilder")))
					fail(QStringLiteral("the Java command list has no ProcessBuilder entry"));
			}

			const QVector<ViolationRule> pythonRules =
			    Violation::defaultRulesFor(QStringLiteral("python"), QStringList{QStringLiteral("py")});
			const QString pythonCode = QStringLiteral("import subprocess\n"
			                                          "subprocess.run([\"pause\"])\n"
			                                          "import re\n"
			                                          "re.compile(\"x\")\n");
			QStringList pythonHits;

			for (const Violation::Match &hit : Violation::checkSource(pythonCode, pythonRules))
				pythonHits << hit.rule.text;

			report << QStringLiteral("python violation hits: %1")
			              .arg(pythonHits.join(QStringLiteral(", ")));

			if (! pythonHits.contains(QStringLiteral("subprocess")))
				fail(QStringLiteral("the python default rules do not cover subprocess"));

			if (pythonHits.contains(QStringLiteral("compile")))
				fail(QStringLiteral("the python default rules flag re.compile()"));

			if (Violation::defaultRulesFor(QStringList{QStringLiteral("cpp")}).size() != rules.size())
				fail(QStringLiteral("non-python compilers did not get the C/C++ default rules"));
		}

		{
			const QString cmd = QStandardPaths::findExecutable(QStringLiteral("cmd"));

			if (cmd.isEmpty()) {
				report << QStringLiteral("exit code check: skipped (no cmd.exe)");
			} else {
				QTemporaryDir work;

				if (! work.isValid()) {
					report << QStringLiteral("exit code check: skipped (no temporary dir)");
				} else {
					const auto runnerConfig = [&](bool requireZero) {
						ProcessRunnerConfig cfg;
						cfg.executableFile = cmd;
						cfg.arguments = QStringLiteral("/c exit 1");
						cfg.workingDirectory = work.path() + QDir::separator();
						cfg.timeLimit = 5000;
						cfg.rawTimeLimit = 5000;
						cfg.memoryLimit = 256;
						cfg.rawMemoryLimit = 256;
						cfg.extraTimeRatio = 0.0;
						cfg.inputFileName = QStringLiteral("ccf.in");
						cfg.outputFileName = QStringLiteral("ccf.out");
						cfg.environment = QProcessEnvironment::systemEnvironment();
						cfg.requireReturnZero = requireZero;
						return cfg;
					};
					const ProcessRunnerResult strict = [&] {
						std::atomic<bool> stop{false};
						auto runner = ProcessRunner::create(runnerConfig(true), stop);
						return runner->run();
					}();
					const ProcessRunnerResult relaxed = [&] {
						std::atomic<bool> stop{false};
						auto runner = ProcessRunner::create(runnerConfig(false), stop);
						return runner->run();
					}();
					report << QStringLiteral("exit code 1: required=%1 relaxed=%2")
					              .arg(static_cast<int>(strict.result))
					              .arg(static_cast<int>(relaxed.result));

					if (strict.result == CannotStartProgram || relaxed.result == CannotStartProgram) {
						report << QStringLiteral("exit code check: skipped (the program could not be started)");
					} else {
						if (strict.result != RunTimeError)
							fail(QStringLiteral("a non-zero exit code was not reported as a runtime error"));

						if (relaxed.result != CorrectAnswer)
							fail(QStringLiteral("the exit code was checked even though the rule is off"));
					}
				}
			}
		}

		{
			QString python = QStandardPaths::findExecutable(QStringLiteral("python"));

			if (python.isEmpty())
				python = QStandardPaths::findExecutable(QStringLiteral("python3"));

			QTemporaryDir work;

			if (python.isEmpty() || ! work.isValid()) {
				report << QStringLiteral("python check: skipped (no python / no temporary dir)");
			} else {
				const QString folder = work.path() + QDir::separator();
				const QString good = folder + QStringLiteral("good.py");
				const QString exception = folder + QStringLiteral("exception.py");
				const QString exitTwo = folder + QStringLiteral("exit2.py");
				const auto writeScript = [](const QString &path, const QString &body) {
					QFile file(path);

					if (! file.open(QIODevice::WriteOnly | QIODevice::Truncate))
						return false;

					return file.write(body.toUtf8()) > 0;
				};
				writeScript(good, QStringLiteral("print(1 + 1)\n"));
				writeScript(exception, QStringLiteral("print(1 // 0)\n"));
				writeScript(exitTwo, QStringLiteral("import sys\nsys.exit(2)\n"));

				const auto runScript = [&](const QString &script) {
					ProcessRunnerConfig cfg;
					cfg.executableFile = python;
					cfg.arguments = QStringLiteral("\"%1\"").arg(script);
					cfg.workingDirectory = folder;
					cfg.timeLimit = 10000;
					cfg.rawTimeLimit = 10000;
					cfg.memoryLimit = 1024;
					cfg.rawMemoryLimit = 1024;
					cfg.extraTimeRatio = 0.0;
					cfg.inputFileName = QStringLiteral("ccf.in");
					cfg.outputFileName = QStringLiteral("ccf.out");
					cfg.environment = QProcessEnvironment::systemEnvironment();
					cfg.requireReturnZero = true;
					std::atomic<bool> stop{false};
					auto runner = ProcessRunner::create(cfg, stop);
					return runner->run();
				};
				const ProcessRunnerResult normal = runScript(good);
				const ProcessRunnerResult raised = runScript(exception);
				const ProcessRunnerResult exitCode = runScript(exitTwo);
				report << QStringLiteral("python: normal=%1 uncaught=%2 sys.exit(2)=%3")
				              .arg(static_cast<int>(normal.result))
				              .arg(static_cast<int>(raised.result))
				              .arg(static_cast<int>(exitCode.result));

				if (normal.result == CannotStartProgram) {
					report << QStringLiteral("python check: skipped (python could not be started)");
				} else {
					if (normal.result != CorrectAnswer)
						fail(QStringLiteral("a python script that ends normally is not 'return 0'"));

					if (raised.result != RunTimeError)
						fail(QStringLiteral("an uncaught python exception is not a runtime error"));

					if (exitCode.result != RunTimeError)
						fail(QStringLiteral("sys.exit(2) is not a runtime error"));
				}
			}
		}

		report.prepend(QStringLiteral("problems=%1").arg(problems));
		QFile out(QString::fromLocal8Bit(argv[2]));

		if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&out);
			stream << report.join(QChar('\n')) << '\n';
		}

		LOG("check-ccf: problems", problems);
		return problems == 0 ? 0 : 1;
	}

	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--check-statement-ui")) {
		QApplication app(argc, argv);
		const QFileInfo dayInfo(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath());
		const QString dayFile = dayInfo.absoluteFilePath();
		QFile day(dayFile);

		if (! day.open(QIODevice::ReadOnly)) {
			LOG("check-statement-ui: cannot open", dayFile);
			return 2;
		}

		Settings settings;
		settings.loadSettings();
		QDir::setCurrent(dayInfo.absolutePath());
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("check-statement-ui: broken contest file", dayFile);
			return 3;
		}

		QString projectTitle;
		QString dayTitle;
		readProjectContext(dayFile, projectTitle, dayTitle);

		StatementEditWidget widget;
		widget.setDayContext(&contest, QFileInfo(dayFile).completeBaseName(), dayTitle,
		                     projectTitle.isEmpty() ? contest.getContestTitle() : projectTitle);

		QStringList report;
		int problems = 0;

		QStringList pdfButtons;

		for (auto *button : widget.findChildren<QPushButton *>()) {
			if (button->text().contains(QStringLiteral("PDF"), Qt::CaseInsensitive))
				pdfButtons << button->text();
		}

		report << QStringLiteral("pdf buttons=%1").arg(pdfButtons.size());

		for (const QString &text : pdfButtons)
			report << QStringLiteral("  button: %1").arg(text);

		if (pdfButtons.size() != 1)
			problems++;

		QLineEdit *nameEdit = nullptr;

		for (auto *edit : widget.findChildren<QLineEdit *>()) {
			if (edit->toolTip().contains(QStringLiteral("<title-day>")))
				nameEdit = edit;
		}

		const QString pattern = QStringLiteral("<title>-<title-day>-<day>");
		const QString dayName = QFileInfo(dayFile).completeBaseName();
		const QString contestTitle = projectTitle.isEmpty() ? contest.getContestTitle() : projectTitle;
		const QString expected =
		    Lemon::common::ResolveStatementPdfName(pattern, dayName, dayTitle, contestTitle) +
		    QStringLiteral(".pdf");

		PdfPreviewWidget *preview = widget.findChild<PdfPreviewWidget *>();
		const auto settle = [] {
			QEventLoop loop;
			QTimer::singleShot(400, &loop, &QEventLoop::quit);
			loop.exec();
		};

		if (! nameEdit) {
			problems++;
			report << QStringLiteral("no PDF name edit found");
		} else {
			nameEdit->setText(pattern);
			settle();
			const QString got = widget.pdfFileName();
			report << QStringLiteral("pattern=%1").arg(contest.getStatementPdfName());
			report << QStringLiteral("resolved=%1").arg(got);
			report << QStringLiteral("expected=%1").arg(expected);
			report << QStringLiteral("preview pdf=%1 pages=%2 info=%3")
			              .arg(preview ? preview->currentPdf() : QStringLiteral("<no preview>"))
			              .arg(preview ? preview->renderedPages() : -1)
			              .arg(preview ? preview->currentInfoText() : QString());

			if (contest.getStatementPdfName() != pattern || got != expected)
				problems++;

			const QString expectedPath = Settings::statementPath() + expected;
			const bool expectedExists = QFileInfo::exists(expectedPath);

			if (! preview || (expectedExists && preview->currentPdf() != expectedPath)) {
				problems++;
				report << QStringLiteral("preview did not follow the file name");
			}

			if (! expectedExists && (! preview || preview->renderedPages() != 0 ||
			                         preview->currentInfoText() != expectedPath)) {
				problems++;
				report << QStringLiteral("preview did not switch to %1").arg(expected);
			}

			const QString missing = QStringLiteral("nope-day1.pdf");
			nameEdit->setText(QStringLiteral("nope-<day>"));
			settle();
			report << QStringLiteral("preview after missing name: pages=%1 info=%2")
			              .arg(preview ? preview->renderedPages() : -1)
			              .arg(preview ? preview->currentInfoText() : QString());

			if (! preview || preview->renderedPages() != 0 ||
			    preview->currentInfoText() != Settings::statementPath() + missing) {
				problems++;
				report << QStringLiteral("preview did not switch to %1").arg(missing);
			}

			nameEdit->setText(pattern);
			settle();
		}

		{
			widget.resize(1400, 900);
			widget.show();
			settle();

			if (! preview) {
				problems++;
				report << QStringLiteral("no PDF preview widget found");
			} else {
				const int css = preview->pageWidthCss();
				const int px = preview->plannedRenderWidthPx();
				report << QStringLiteral("preview sharpness: css=%1 px=%2 dpr=%3")
				              .arg(css)
				              .arg(px)
				              .arg(preview->devicePixelRatioF());

				if (px <= css || px < 640)
					problems++;

				QPixmap page(1500, 2121); 
				PdfPreviewWidget::fitPagePixmap(page, 600);
				const double dpr = page.devicePixelRatio();
				report << QStringLiteral("page pixmap: dpr=%1 css=%2")
				              .arg(dpr, 0, 'f', 2)
				              .arg(page.deviceIndependentSize().width(), 0, 'f', 0);

				if (qAbs(dpr - 2.5) > 0.01 || qAbs(page.deviceIndependentSize().width() - 600.0) > 0.01)
					problems++;

				const bool canRender =
				    ! QStandardPaths::findExecutable(QStringLiteral("pdftocairo")).isEmpty() ||
				    ! QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty();
				const QString realPdf = Settings::statementPath() + widget.pdfFileName();
				report << QStringLiteral("real pdf: %1 exists=%2 renderer=%3")
				              .arg(widget.pdfFileName())
				              .arg(QFileInfo::exists(realPdf) ? 1 : 0)
				              .arg(canRender ? 1 : 0);

				if (canRender && QFileInfo::exists(realPdf)) {
					for (int attempt = 0; attempt < 30 && preview->renderedPages() == 0; ++attempt) {
						QEventLoop loop;
						QTimer::singleShot(200, &loop, &QEventLoop::quit);
						loop.exec();
					}

					int pages = 0;
					double pageDpr = 0.0;
					double pageCss = 0.0;

					for (auto *label : preview->findChildren<QLabel *>()) {
						const QPixmap shot = label->pixmap();

						if (shot.isNull())
							continue;

						++pages;
						pageDpr = shot.devicePixelRatio();
						pageCss = shot.deviceIndependentSize().width();
					}

					report << QStringLiteral("rendered page: pages=%1 dpr=%2 css=%3 (wanted %4)")
					              .arg(pages)
					              .arg(pageDpr, 0, 'f', 2)
					              .arg(pageCss, 0, 'f', 0)
					              .arg(preview->pageWidthCss());

					if (pages == 0 || pageDpr <= 1.0 ||
					    qAbs(pageCss - preview->pageWidthCss()) > preview->pageWidthCss() * 0.05)
						problems++;
				}

				widget.hide();
			}
		}

		{
			ExportWidget exportWidget;
			exportWidget.setDayFile(dayFile);
			exportWidget.setContest(&contest);
			exportWidget.setDefaultStatementFile(widget.pdfFileName());
			exportWidget.refresh();

			auto *statementList = exportWidget.findChild<QListWidget *>(QStringLiteral("statementList"));
			QStringList items;
			QStringList checked;

			if (statementList)
				for (int i = 0; i < statementList->count(); i++) {
					const QListWidgetItem *item = statementList->item(i);
					items << item->text();

					if (item->checkState() == Qt::Checked)
						checked << item->text();
				}

			report << QStringLiteral("statement list: %1 (checked: %2)")
			              .arg(items.join(QStringLiteral(", ")), checked.join(QStringLiteral(", ")));

			const QString expected = widget.pdfFileName();
			const auto matchesExpected = [&expected](const QString &text) {
				return text == expected || text.startsWith(expected + QStringLiteral(" ("));
			};
			bool offered = false;
			bool selected = false;

			for (const QString &item : items)
				if (matchesExpected(item))
					offered = true;

			for (const QString &item : checked)
				if (matchesExpected(item))
					selected = true;

			if (! statementList || ! offered) {
				problems++;
				report << QStringLiteral("the statement list does not offer %1").arg(expected);
			} else if (! selected) {
				problems++;
				report << QStringLiteral("the statement list does not check %1 by default").arg(expected);
			}
		}

		report.prepend(QStringLiteral("problems=%1").arg(problems));

		QFile out(QDir(QFileInfo(dayFile).absolutePath())
		              .absoluteFilePath(QStringLiteral("statement-ui-check.txt")));

		if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&out);
			stream << report.join(QChar('\n')) << '\n';
		}

		LOG("check-statement-ui: problems", problems);
		return problems == 0 ? 0 : 1;
	}

	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--self-test")) {
		QApplication app(argc, argv);
		Q_INIT_RESOURCE(resource);
		QStringList report;
		int problems = 0;
		const auto fail = [&](const QString &line) {
			problems++;
			report << (QStringLiteral("FAIL ") + line);
		};

		report << QStringLiteral("version=%1").arg(QString(LEMON_VERSION_STRING));
		report << QStringLiteral("exe=%1").arg(QCoreApplication::applicationDirPath());
		report << QStringLiteral("platform=%1").arg(QGuiApplication::platformName());
		report << QStringLiteral("style=%1").arg(QApplication::style()->objectName());

		if (QGuiApplication::platformName().isEmpty())
			fail(QStringLiteral("platforms/qwindows.dll 没加载起来（平台插件缺失）"));

		QStringList formats;

		for (const QByteArray &format : QImageReader::supportedImageFormats())
			formats << QString::fromLatin1(format);

		report << QStringLiteral("image formats=%1").arg(formats.join(QChar(',')));

		if (! formats.contains(QStringLiteral("svg")))
			fail(QStringLiteral("imageformats/qsvg.dll 缺失，读不了 SVG"));

		const QStringList icons{QStringLiteral(":/icon/icon.png"), QStringLiteral(":/icon/acrobat.svg"),
		                        QStringLiteral(":/icon/document-save.svg"),
		                        QStringLiteral(":/icon/view-refresh.svg"),
		                        QStringLiteral(":/icon/problem-configure.svg"),
		                        QStringLiteral(":/logo/splash2.png")};
		int loaded = 0;

		for (const QString &path : icons)
			if (! QIcon(path).pixmap(16, 16).isNull())
				loaded++;

		report << QStringLiteral("icons=%1/%2").arg(loaded).arg(icons.size());

		if (loaded != icons.size())
			fail(QStringLiteral("有图标加载不出来（iconengines/qsvgicon.dll 或资源缺失）"));

		const QString templates = StatementBuilder::templateRoot();
		report << QStringLiteral("statement templates=%1").arg(templates);

		if (templates.isEmpty())
			fail(QStringLiteral("找不到题面模板 statement-templates/"));

		const QStringList languages = QDir(QStringLiteral(":/translation/")).entryList({QStringLiteral("*.qm")});
		report << QStringLiteral("embedded translations=%1").arg(languages.join(QChar(',')));

		if (languages.isEmpty())
			fail(QStringLiteral("没有内嵌翻译"));

		{
			LemonLime window;
			const int actions = window.findChildren<QAction *>().size();
			report << QStringLiteral("main window=ok actions=%1 central=%2")
			              .arg(actions)
			              .arg(window.centralWidget() ? QStringLiteral("yes") : QStringLiteral("no"));

			if (! window.centralWidget() || actions < 50)
				fail(QStringLiteral("主窗口没建完整（菜单 / 工具条缺东西）"));
		}

		report.prepend(QStringLiteral("problems=%1").arg(problems));

		QFile out(QString::fromLocal8Bit(argv[2]));

		if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&out);
			stream << report.join(QChar('\n')) << '\n';
		}

		LOG("self-test: problems", problems);
		return problems == 0 ? 0 : 1;
	}

	Lemon::LemonBaseApplication app(argc, argv);

	app.Initialize();

	if (app.sendMessage("")) {
		app.activeWindow();
		return 0;
	}

#ifdef Q_OS_LINUX
#endif
#ifdef Q_OS_WIN32
	QFont fonts;
	fonts.setFamily("Microsoft YaHei");
	fonts.setHintingPreference(QFont::PreferNoHinting);
	SingleApplication::setFont(fonts);
#endif
#ifdef Q_OS_MAC
#endif
	Q_INIT_RESOURCE(resource);
	QPixmap pixmap(":/logo/splash2.png");
	QSplashScreen screen(pixmap.scaled(450, 191, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	LemonLime w;
	qint64 startTime = QDateTime::currentMSecsSinceEpoch();
	int splashTime = w.getSplashTime();

	if (splashTime > 0) {
		screen.show();

		do {
			SingleApplication::processEvents();
		} while (QDateTime::currentMSecsSinceEpoch() - startTime <= splashTime);

		screen.finish(&w);
	}

	w.activateWindow();
	w.show();
	w.welcome();
	return app.exec();
}
