/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lemon.h"
#include "admission/admissionassign.h"
#include "admission/admissiongenerator.h"
#include "admission/admissionnotes.h"
#include "admission/admissionproject.h"
#include "admission/admissiontemplate.h"
#include "admission/admissionwidget.h"
#include "admission/csveditordialog.h"
#include "admission/idruledialog.h"
#include "admission/notesdialog.h"
#include "admission/seatdialog.h"
#include "admission/venuedialog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
//
#include "base/LemonBase.hpp"
#include "base/LemonBaseApplication.hpp"
#include "base/LemonLog.hpp"
#include "base/LemonUtils.hpp"
#include "base/settings.h"
#include "component/exportutil/exportutil.h"
#include "core/contest.h"
#include "core/dayproject.h"
#include "core/packagebuilder.h"
#include "core/processrunner.h"
#include "core/statementbuilder.h"
#include "core/taskjudger.h"
#include "core/violation.h"
#include "exportwidget.h"
#include "pdfpreview.h"
#include "resultviewer.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "statementeditwidget.h"
#include "statisticsbrowser.h"
//
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
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <chrono>
#include <utility>

#define LEMON_MODULE_NAME "Main"

namespace {
	/// 比赛日文件旁边的工程文件（contest.conf）：用来拿「比赛标题 / 比赛日标题」。
	/// 找不到时两个参数保持空串（模板里的占位符会退回比赛日名）。
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

	/// 界面「题面」选项卡会导出成什么文件名：比赛日里存的模板 + 占位符替换。
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
} // namespace

void initLogger() {
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	console_sink->set_level(spdlog::level::warn);
	QDir logDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QDir::separator() +
	            "logs");
	logDir.mkpath(".");
	// retain last 30 days logs
	auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
	    (logDir.path() + QDir::separator() + "lemonlime-log.txt").toStdString(), 0, 0, false, 30);
	file_sink->set_level(spdlog::level::trace);
	Lemon::base::logger =
	    std::make_shared<spdlog::logger>(spdlog::logger("lemonlime", {console_sink, file_sink}));
	// 每条日志立即落盘：万一崩溃，日志里能看到最后做了什么
	Lemon::base::logger->flush_on(spdlog::level::trace);
	spdlog::flush_every(std::chrono::seconds(5));
}

int main(int argc, char *argv[]) {

	QCoreApplication::setApplicationName("Lemonlime");

	initLogger();

	// 隐藏入口：命令行直接构建题面 PDF（图形界面里的「题面 -> 导出 PDF」用的是同一套引擎）。
	// 用法：lemon.exe --build-statement <statement.md> <输出前缀> <ccpc|noi|noi new>
	if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--build-statement")) {
		QCoreApplication app(argc, argv);
		StatementBuilder builder;
		builder.setSourceFile(QString::fromLocal8Bit(argv[2]));
		builder.setOutputBase(QString::fromLocal8Bit(argv[3]));
		builder.setTemplate(QString::fromLocal8Bit(argv[4]));
		// 图形子系统程序看不到控制台输出，这里把日志写到输出旁边的文件里。
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

	// 隐藏入口：命令行导出某个比赛日的成绩表（显示用的表格 / HTML / CSV），
	// 便于批量导出，也方便自动化验证列结构。
	// 用法：lemon.exe --export-score <比赛日.cdf> <输出前缀>
	//   生成 <前缀>.html、<前缀>.csv，以及列结构报告 <前缀>.columns.txt
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
		// Settings 的构造函数不会读配置，而下面 refreshViewer() 要用到颜色主题，
		// 必须先 loadSettings()，否则 colorThemeList 是空的。
		settings.loadSettings();
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("export-score: broken contest file", dayFile);
			return 3;
		}

		// 直接把界面上那张成绩表的列结构打印出来
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

	// 隐藏入口：命令行「导出成绩 / 导出统计」到默认位置（比赛日目录下的 reports/），
	// 和界面上那两个导出走的是同一段逻辑，方便自动化验证导出结果。
	// 用法：lemon.exe --export-reports <比赛日.cdf>
	//   生成 <比赛日目录>/reports/result.html、statistics.html；
	//   启用赛区时额外生成 result-<赛区>.html、statistics-<赛区>.html
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

		// exportResult() / exportStatistics() 固定在「当前工作目录」的 reports/ 下导出，
		// 图形界面是在打开比赛日时把工作目录切过去的，这里手动切一下。
		QDir::setCurrent(QFileInfo(dayFile).absolutePath());
		ExportUtil::exportResult(nullptr, &contest);
		StatisticsBrowser::exportStatistics(nullptr, &contest);
		return 0;
	}

	// 隐藏入口：命令行编译某个比赛日的准考证 PDF（与界面里「准考证 -> 生成」同一套引擎）。
	// 用法：lemon.exe --build-admission <比赛日.cdf> [赛区 ...]
	//   不带赛区就把 admission/ 里的名单全部生成一遍；PDF 固定写到 <比赛日>/dist/admission/ 下。
	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--build-admission")) {
		QApplication app(argc, argv);
		const QString dayFile = QString::fromLocal8Bit(argv[2]);
		const QString dayFolder = QFileInfo(dayFile).absolutePath();
		// 标题上下文与界面一致：有工程文件（contest.conf）就用里面的比赛标题 / 比赛日标题。
		// 注意要在 chdir 之前读，否则相对的 dayFile 就找不到自己了。
		QString projectTitle;
		QString dayTitle;
		readProjectContext(dayFile, projectTitle, dayTitle);
		QFile day(dayFile);

		if (! day.open(QIODevice::ReadOnly)) {
			LOG("build-admission: cannot open", dayFile);
			return 2;
		}

		Settings settings;
		settings.loadSettings();
		QDir::setCurrent(dayFolder);
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("build-admission: broken contest file", dayFile);
			return 3;
		}

		AdmissionProject admission;
		QString loadError;

		if (! admission.load(contest.getRegionEnabled(), &loadError))
			LOG("build-admission: load warning:", loadError);

		AdmissionGenerator generator;
		QObject::connect(&generator, &AdmissionGenerator::logMessage,
		                 [](const QString &line) { LOG("admission:", line); });
		QStringList regions;

		for (int i = 3; i < argc; i++)
			regions << QString::fromLocal8Bit(argv[i]);

		QString error;
		const int failed = generator.generate(admission, regions,
		                                      dayTitle.isEmpty() ? projectTitle : dayTitle, &error);

		if (failed < 0) {
			LOG("build-admission: failed:", error);
			return 1;
		}

		return failed == 0 ? 0 : 1;
	}

	// 隐藏入口：命令行跑一遍评测（与界面「重测」同一段逻辑）。
	// 用法：lemon.exe --judge-day <比赛日.cdf> [--contestant <选手名>] [--task <题目标题>] [--rules]
	// 默认不查违规 / 命名规则（选手目录名往往不合规范，直接测更方便）；加 --rules 按规则查，
	// 被取消测试的选手也会像界面那样上报它跳过的工时。
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
		// 被取消测试的选手：记下上报的进度，方便对照「进度条该跳过多少」。
		QObject::connect(&contest, &Contest::contestantSkipped,
		                 [](const QString &name, const QString &, const QString &, int progress) {
			                 LOG("judge-day: skipped", name, "progress", progress);
		                 });
		contest.judge(lists, ignoreRules);

		// 每道题的结果打一行日志：命令行跑评测时不用开界面就能看到判定（自检也靠它）。
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

	// 隐藏入口：验证「选手的测试被取消时，后台成绩表立刻变样」。
	// 用法：lemon.exe --check-live-skip <比赛日.cdf>
	//   先把成绩表刷成「谁都没违规」的样子（把所有人的取消状态清掉再刷新），
	//   然后按规则评测：contestantSkipped 发出的那一刻立刻读一遍表格里那一格的背景色和
	//   悬停提示，确认表格已经变成「测试被取消」的样子（而不是等下一个正常选手测完）。
	//   报告写到 <比赛日目录>/live-skip-check.txt，全部符合预期返回 0。
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

		// 先把所有人当普通人，这样「测试前」的成绩表是正常颜色。
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

		contest.judge(lists, /*ignoreRules=*/false);

		report.prepend(QStringLiteral("skipped=%1 stale=%2").arg(skipped).arg(stale));

		QFile out(QStringLiteral("live-skip-check.txt"));

		if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&out);
			stream << report.join(QChar('\n')) << '\n';
		}

		LOG("check-live-skip: skipped", skipped, "stale", stale);

		// 一个被跳过的选手都没有（规则没生效）也算失败：那这次验证什么也没验到。
		if (skipped == 0 || stale != 0)
			return 1;

		return 0;
	}

	// 隐藏入口：命令行把比赛日打包成 .zip（与界面「导出」选项卡同一段逻辑）。
	// 用法：lemon.exe --export-package <比赛日.cdf> [--kind contestant|testdata] [--wrap] [--nested]
	//        [--no-per-task] [--no-structure] [--samples] [--password <密码>]
	//        [--statement <statement 下的文件名>]
	//   生成 <比赛日目录>/dist/export/<比赛日名>.zip
	//   --statement 只在选手目录包里有效；不给时用比赛日里存的模板算出来的文件名。
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
		QString statement;
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
			else if (option == QLatin1String("--statement") && i + 1 < argc)
				statement = QString::fromLocal8Bit(argv[++i]);
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
		// 没指定题面文件时用「比赛日文件里存的模板 + 比赛日上下文」算出来的名字。
		builder.setStatementFile(statement.isEmpty() ? resolvedStatementFileName(dayFile, contest) : statement);
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

	// 隐藏入口：算出「题面」选项卡会导出成什么文件名（不真的编译）。
	// 用法：lemon.exe --pdf-name <比赛日.cdf>
	//   把模板与替完占位符的名字写到 <比赛日目录>/statement-name.txt，供脚本核对。
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

	// 隐藏入口：CCF 规范相关判定逻辑自检（不需要比赛数据）。
	// 用法：lemon.exe --check-ccf <报告文件>
	//   检查《关于NOI系列赛编程语言使用限制的规定》里评测系统能管的部分：
	//   多源文件的后缀选取顺序（编程通则 1）、违规默认规则覆盖严禁清单（编程通则 4）、
	//   程序必须返回 0（编程通则 3，真的跑一个进程验证）。
	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--check-ccf")) {
		QApplication app(argc, argv);
		QStringList report;
		int problems = 0;
		const auto fail = [&](const QString &line) {
			problems++;
			report << (QStringLiteral("FAIL ") + line);
		};

		// 1) 通用设置：默认要求 return 0
		{
			Settings settings;
			report << QStringLiteral("require return zero (default): %1")
			              .arg(settings.getRequireReturnZero() ? 1 : 0);

			if (! settings.getRequireReturnZero())
				fail(QStringLiteral("the CCF rule 'main must return 0' is off by default"));
		}

		// 2) 编程通则 1：源文件的选取顺序来自**编译器自己声明的后缀列表**，
		//    跨语言则是编译器列表的顺序（gcc 排在 g++ 前面 → .c 优先于 .cpp）。
		{
			// 一个编译器声明了多个后缀（g++ 默认就是 cpp;cc;cxx）时按声明顺序取
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

			// gcc 只认 .c、g++ 认 cpp;cc;cxx：编译器列表 [gcc, g++] 就是 .c → .cpp 的优先级
			const QStringList cOnly{QStringLiteral("c")};
			QString picked;
			QString pickedBy;
			const QList<QPair<QString, QStringList>> compilers{
			    {QStringLiteral("gcc"), cOnly}, {QStringLiteral("g++"), gpp},
			    {QStringLiteral("fpc"), {QStringLiteral("pas"), QStringLiteral("pp")}}};

			for (const auto &compiler : compilers) {
				// 只认这个编译器声明过的后缀：先看有没有能匹配的候选文件
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

			// 后缀不在任何编译器声明里就排最后
			if (TaskJudger::suffixRank(gpp, QStringLiteral("cake.py")) != gpp.size())
				fail(QStringLiteral("an unknown suffix must rank last"));
		}

		// 3) 编程通则 4：默认违规规则要覆盖严禁的操作（网络 / 进程 / 线程 / 汇编 / 编译选项）
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

			// 真命中：调用 system() / 起线程 / 访问网络
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

			// 不误伤：注释里的名字不算；普通函数名（connect）不在清单里
			const QString clean = QStringLiteral("// system(\n"
			                                     "int connect(int a, int b) { return a + b; }\n"
			                                     "int main() { return connect(1, 2) - 3; }\n");
			const QVector<Violation::Match> cleanHits = Violation::checkSource(clean, rules);
			report << QStringLiteral("violations in clean code: %1").arg(cleanHits.size());

			if (! cleanHits.isEmpty())
				fail(QStringLiteral("the default rules flag innocent code"));
		}

		// 4) 编程通则 3：程序必须以 0 退出（真的跑一个进程看结果）
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

		// 5) 解释型语言（Python）里的「返回 0」：看解释器进程的退出码。
		//    脚本正常跑完 → 0；未捕获异常 / sys.exit(非 0) → 非 0 → 运行时错误。
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
				// 正常跑完（Python 没写 return 0 也天生是 0）
				writeScript(good, QStringLiteral("print(1 + 1)\n"));
				// 未捕获异常（ZeroDivisionError）→ 解释器退出码 1
				writeScript(exception, QStringLiteral("print(1 // 0)\n"));
				// 显式退出：写 0 就是「返回 0」，非 0 就是没正常结束
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

	// 隐藏入口：检查「题面」选项卡的两件事（不编译 PDF）。
	// 用法：lemon.exe --check-statement-ui <比赛日.cdf>
	//   1) 工具条上「导出 PDF」「打开 PDF」确实只剩一个按钮；
	//   2) 在文件名框里写模板，会存进比赛日，并且算出来的名字与占位符一致。
	//   结果写到 <比赛日目录>/statement-ui-check.txt，全部符合预期返回 0。
	if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--check-statement-ui")) {
		QApplication app(argc, argv);
		// 先换成绝对路径：下面会把工作目录切到比赛日目录，相对路径就不能再用了。
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
		widget.setDayContext(&contest, QFileInfo(dayFile).completeBaseName(), dayTitle, projectTitle);

		QStringList report;
		int problems = 0;

		// 1) 工具条上只能有一个提到 PDF 的按钮（「导出 PDF」与「打开 PDF」已经并成一个）。
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

		// 2) 在文件名框里写模板：要存进比赛日，并且按占位符算出名字。
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

		// 预览是异步换文件的（去抖 200ms）：跑一小段事件循环再读状态。
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

			// 文件名改了，预览就要指向新名字对应的那个文件（文件还不存在时，预览会
			// 清空并在信息行里写出它在等哪个文件，下面单独查）。
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

			// 名字对应的 PDF 不存在时：清空预览，并在信息行写清楚在等哪个文件。
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

		// 2b) 预览清晰度：按「预览窗宽度 × 设备像素比 + 一点超采样」渲染，
		//     页图按 css 宽度显示（像素比 = 图片宽 ÷ 显示宽），Qt 不再放大它。
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

				// 出图必须比显示尺寸更细（原来是按 72dpi 出图、1:1 贴上去，缩放屏上一放大就糊）
				if (px <= css || px < 640)
					problems++;

				// 页图按 css 宽显示：像素比 = 图片宽 ÷ css 宽，Qt 不会再去插值放大
				QPixmap page(1500, 2121); // 假装是一张 A4 @ ~180dpi 的渲染结果
				PdfPreviewWidget::fitPagePixmap(page, 600);
				const double dpr = page.devicePixelRatio();
				report << QStringLiteral("page pixmap: dpr=%1 css=%2")
				              .arg(dpr, 0, 'f', 2)
				              .arg(page.deviceIndependentSize().width(), 0, 'f', 0);

				if (qAbs(dpr - 2.5) > 0.01 || qAbs(page.deviceIndependentSize().width() - 600.0) > 0.01)
					problems++;

				// 真有 PDF、又装了渲染器的话，等它渲染完，检查真的页图是不是也按新规则贴上去的
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

					// 页图必须比显示尺寸更细（dpr > 1），显示宽度就是预览窗宽度（允许滚动条之类的几像素误差）
					if (pages == 0 || pageDpr <= 1.0 ||
					    qAbs(pageCss - preview->pageWidthCss()) > preview->pageWidthCss() * 0.05)
						problems++;
				}

				widget.hide();
			}
		}

		// 3) 导出选项卡里的「题面文件」下拉框：列 statement/ 下的文件，默认选模板算出的那个。
		{
			ExportWidget exportWidget;
			exportWidget.setDayFile(dayFile);
			exportWidget.setContest(&contest);
			exportWidget.setDefaultStatementFile(widget.pdfFileName());
			exportWidget.refresh();

			QComboBox *statementBox = nullptr;

			for (auto *combo : exportWidget.findChildren<QComboBox *>()) {
				if (combo->toolTip().contains(QStringLiteral("statement/")))
					statementBox = combo;
			}

			QStringList items;

			if (statementBox)
				for (int i = 0; i < statementBox->count(); i++)
					items << statementBox->itemText(i);

			report << QStringLiteral("statement combo: %1").arg(items.join(QStringLiteral(", ")));

			// 默认题面还没导出时下拉框里会有个「… (missing)」的条目，也算提供了。
			bool offered = false;

			for (const QString &item : items)
				if (item == expected || item.startsWith(expected + QStringLiteral(" (")))
					offered = true;

			if (! statementBox || ! offered) {
				problems++;
				report << QStringLiteral("statement combo does not offer %1").arg(expected);
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

#if 0
	// 旧版自检（编辑器改版前的实现，留作参考）
	if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--check-admission-ui-old")) {
		QApplication app(argc, argv);
		const QString dayFile = QString::fromLocal8Bit(argv[2]);
		const QString reportFile = QString::fromLocal8Bit(argv[3]);
		QStringList report;
		int problems = 0;
		auto fail = [&report, &problems](const QString &what) {
			++problems;
			report << QStringLiteral("FAILED: ") + what;
		};

		QFile day(dayFile);

		if (! day.open(QIODevice::ReadOnly)) {
			LOG("check-admission-ui: cannot open", dayFile);
			return 2;
		}

		Settings settings;
		settings.loadSettings();
		const QString dayFolder = QFileInfo(dayFile).absolutePath();
		const QString dayBase = QFileInfo(dayFile).completeBaseName();
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("check-admission-ui: broken contest file", dayFile);
			return 3;
		}

		QTemporaryDir scratch;

		if (! scratch.isValid()) {
			LOG("check-admission-ui: cannot create a temporary directory");
			return 4;
		}

		QDir::setCurrent(scratch.path());
		const QString region = QStringLiteral("HN");

		// 1) 选项卡：能列出赛区、表格列数对
		{
			AdmissionWidget widget;
			widget.setContest(&contest);
			widget.setDayContext(dayBase, QStringLiteral("Day 1"), contest.getContestTitle());
			widget.refresh();
			QTableWidget *table = widget.findChild<QTableWidget *>();
			report << QStringLiteral("tab table: rows=%1 columns=%2")
			              .arg(table ? table->rowCount() : -1)
			              .arg(table ? table->columnCount() : -1);

			if (! table || table->columnCount() != 5)
				fail(QStringLiteral("admission tab has no 5-column table"));
		}

		AdmissionBuilder builder;
		builder.setContest(&contest);
		builder.setDayContext(dayBase, QStringLiteral("Day 1"), contest.getContestTitle());

		// 2) 名单：写下去再读回来
		{
			const QStringList header = AdmissionBuilder::defaultListHeader();
			QList<QStringList> rows;
			rows << QStringList{QStringLiteral("张三"), QStringLiteral("HN-S01192"), QStringLiteral("26"),
			                    QString()};
			rows << QStringList{QStringLiteral("李四"), QStringLiteral("HN-S01193"), QStringLiteral("27"),
			                    QString()};
			QString error;

			if (! builder.writeList(region, header, rows, error))
				fail(QStringLiteral("writeList: ") + error);

			QStringList readHeader;
			QList<QStringList> readRows;

			if (! builder.readList(region, readHeader, readRows, error))
				fail(QStringLiteral("readList: ") + error);

			if (readHeader != header || readRows.size() != 2 ||
			    readRows.value(0).value(0) != QStringLiteral("张三"))
				fail(QStringLiteral("list round-trip"));

			report << QStringLiteral("list round-trip: header=%1 rows=%2 first=%3")
			              .arg(readHeader.join(QChar('/')))
			              .arg(readRows.size())
			              .arg(readRows.value(0).value(0));
		}

		// 3) 注意事项：赛区信息 + 两节通告的来回
		{
			QMap<QString, QString> info;
			info.insert(QStringLiteral("测试时间"), QStringLiteral("2026-09-19 14:30:00"));
			info.insert(QStringLiteral("考点"), QStringLiteral("长沙市"));
			info.insert(QStringLiteral("考场"), QStringLiteral("08考场"));
			QString error;

			if (! builder.writeNotes(region, info, QStringLiteral("1. 甲\n2. 乙"), QStringLiteral("1. 丙"),
			                         error))
				fail(QStringLiteral("writeNotes: ") + error);

			QMap<QString, QString> readInfo;
			QString notes;
			QString general;

			if (! builder.readNotes(region, readInfo, notes, general, error))
				fail(QStringLiteral("readNotes: ") + error);

			if (readInfo != info || notes != QStringLiteral("1. 甲\n2. 乙") ||
			    general != QStringLiteral("1. 丙"))
				fail(QStringLiteral("notes round-trip"));

			report << QStringLiteral("notes round-trip: fields=%1 notes=%2 general=%3")
			              .arg(readInfo.size())
			              .arg(notes.simplified())
			              .arg(general.simplified());
		}

		// 4) 三个内置编辑器：能读出东西就算过
		{
			AdmissionListDialog dialog(&builder, region);
			QTableWidget *table = dialog.findChild<QTableWidget *>();
			report << QStringLiteral("list editor: rows=%1 columns=%2")
			              .arg(table ? table->rowCount() : -1)
			              .arg(table ? table->columnCount() : -1);

			if (! table || table->rowCount() != 2 || table->columnCount() != 4)
				fail(QStringLiteral("list editor did not load the list"));
		}
		{
			AdmissionNotesDialog dialog(&builder, region);
			QTableWidget *table = dialog.findChild<QTableWidget *>();
			const QList<QPlainTextEdit *> edits = dialog.findChildren<QPlainTextEdit *>();
			report << QStringLiteral("notes editor: fields=%1 text boxes=%2")
			              .arg(table ? table->rowCount() : -1)
			              .arg(edits.size());

			if (! table || table->rowCount() < 3 || edits.size() != 2)
				fail(QStringLiteral("notes editor did not load the notes"));
			else if (! edits.at(0)->toPlainText().contains(QStringLiteral("甲")))
				fail(QStringLiteral("notes editor lost the 注意事项 text"));
		}
		{
			QString error;
			const QString path = AdmissionBuilder::ensureTemplate(&error);

			if (path.isEmpty())
				fail(QStringLiteral("ensureTemplate: ") + error);

			AdmissionTextDialog dialog(QStringLiteral("template"), path);
			QPlainTextEdit *edit = dialog.findChild<QPlainTextEdit *>();
			const int chars = edit ? edit->toPlainText().size() : -1;
			report << QStringLiteral("template editor: chars=%1").arg(chars);

			if (! edit || chars < 100 ||
			    ! edit->toPlainText().contains(QStringLiteral("\\begin{document}")))
				fail(QStringLiteral("template editor did not load the template"));
		}

		report.prepend(QStringLiteral("problems=%1").arg(problems));

		QFile out(QDir(dayFolder).absoluteFilePath(reportFile));

		if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&out);
			stream << report.join(QChar('\n')) << '\n';
		}

		LOG("check-admission-ui: problems", problems);
		return problems == 0 ? 0 : 1;
	}
#endif

	// 隐藏入口：检查准考证的选项卡、数据层与两个编辑窗口（在临时目录里跑，不动比赛日目录）。
	// 用法：lemon.exe --check-admission-ui <比赛日.cdf> <报告文件>
	if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--check-admission-ui")) {
		QApplication app(argc, argv);
		const QString dayFile = QString::fromLocal8Bit(argv[2]);
		const QString reportFile = QString::fromLocal8Bit(argv[3]);
		QStringList report;
		int problems = 0;
		auto fail = [&report, &problems](const QString &what) {
			++problems;
			report << QStringLiteral("FAILED: ") + what;
		};

		QFile day(dayFile);

		if (! day.open(QIODevice::ReadOnly)) {
			LOG("check-admission-ui: cannot open", dayFile);
			return 2;
		}

		Settings settings;
		settings.loadSettings();
		const QString dayFolder = QFileInfo(dayFile).absolutePath();
		Contest contest(nullptr);
		contest.setSettings(&settings);

		if (contest.readFromJson(QJsonDocument::fromJson(day.readAll()).object()) == -1) {
			LOG("check-admission-ui: broken contest file", dayFile);
			return 3;
		}

		QTemporaryDir scratch;

		if (! scratch.isValid()) {
			LOG("check-admission-ui: cannot create a temporary directory");
			return 4;
		}

		QDir::setCurrent(scratch.path());

		// 1) 内置模板：存在、锁定区结构对、字体目录在
		{
			const QString text = AdmissionTemplate::source();
			QString error;

			if (text.isEmpty())
				fail(QStringLiteral("built-in template not found"));
			else if (! AdmissionTemplate::validate(text, &error))
				fail(QStringLiteral("template: ") + error);

			report << QStringLiteral("template: locked rows=%1 placeholders ok, bundled fonts=%2")
			              .arg(AdmissionTemplate::lockedRowLabels().size())
			              .arg(AdmissionTemplate::fontDir().isEmpty() ? 0 : 1);
		}

		// 2) 数据层：建赛区 → 写名单 / 赛区信息 / 通告 → 重新读回来
		{
			AdmissionProject project;
			QString error;

			if (! project.createRegion(QStringLiteral("A赛区"), &error))
				fail(QStringLiteral("createRegion: ") + error);

			if (! project.load(true, &error))
				fail(QStringLiteral("load: ") + error);

			AdmissionRegion *region = project.find(QStringLiteral("A赛区"));

			if (! region) {
				fail(QStringLiteral("region not discovered from regions/"));
			} else {
				project.examTime = QStringLiteral("2026-09-19 14:30:00");
				// 准考证号策略也是比赛日级的配置，跟着 config.json 走
				project.idTemplate = QStringLiteral("<section>-<number><number>");
				project.idCharSources = QStringList{QStringLiteral("name")};
				// 自定义列（锁定行之外）会在生成时印成额外行，这里顺带验一遍它能不能原样过一遍磁盘。
				region->table.header << QStringLiteral("考场位置");
				region->table.rows << QStringList{QStringLiteral("张三"), QStringLiteral("A-001"),
				                                  QStringLiteral("长沙市"), QStringLiteral("08考场"),
				                                  QStringLiteral("26"), QString(),
				                                  QStringLiteral("<row>排")};
				region->notes = QStringLiteral("1. 甲");
				project.contestNotes = QStringLiteral("1. 丙");
				// 考点 → 考场（含容量）：存 regions/<赛区>/rooms.json，排座位唯一根据
				AdmissionVenue venue1;
				venue1.name = QStringLiteral("考点一");
				venue1.rooms << AdmissionRoom{QStringLiteral("101"), 3}
				             << AdmissionRoom{QStringLiteral("102"), 2};
				AdmissionVenue venue2;
				venue2.name = QStringLiteral("考点二");
				venue2.rooms << AdmissionRoom{QStringLiteral("201"), 4};
				region->venues = {venue1, venue2};

				if (! project.save(&error))
					fail(QStringLiteral("save: ") + error);
			}

			AdmissionProject again;

			if (! again.load(true, &error))
				fail(QStringLiteral("reload: ") + error);

			const AdmissionRegion *check = again.find(QStringLiteral("A赛区"));

			if (! check || check->table.rows.size() != 1 || check->table.cell(0, 0) != QStringLiteral("张三") ||
			    check->table.cell(0, 2) != QStringLiteral("长沙市") ||
			    check->table.cell(0, 4) != QStringLiteral("26") ||
			    check->table.header.indexOf(QStringLiteral("考场位置")) !=
			        AdmissionTable::builtinColumns().size() ||
			    check->table.cell(0, AdmissionTable::builtinColumns().size()) != QStringLiteral("<row>排") ||
			    check->notes.trimmed() != QStringLiteral("1. 甲") ||
			    again.examTime != QStringLiteral("2026-09-19 14:30:00") ||
			    again.idTemplate != QStringLiteral("<section>-<number><number>") ||
			    again.idCharSources != QStringList{QStringLiteral("name")} ||
			    again.regions.size() != 1 || again.regions.at(0).venues.size() != 2 ||
			    again.regions.at(0).venues.at(0).rooms.value(1).capacity != 2 ||
			    again.regions.at(0).roomCount() != 3 || again.regions.at(0).totalCapacity() != 9 ||
			    again.contestNotes.trimmed() != QStringLiteral("1. 丙"))
				fail(QStringLiteral("admission project round-trip"));

			report << QStringLiteral("project round-trip: regions=%1 rows=%2 examTime=%3")
			              .arg(again.regions.size())
			              .arg(check ? check->table.rows.size() : -1)
			              .arg(again.examTime);
			report << QStringLiteral("id rule: %1").arg(again.idTemplate);
			report << QStringLiteral("venues: %1 / rooms=%2 / capacity=%3")
			              .arg(again.regions.value(0).venues.size())
			              .arg(again.regions.value(0).roomCount())
			              .arg(again.regions.value(0).totalCapacity());
		}

		// 3) 选项卡：4 列的赛区表，能列出来自磁盘的赛区
		{
			AdmissionWidget widget;
			widget.setContest(&contest);
			widget.setDayContext(QFileInfo(dayFile).completeBaseName(), QStringLiteral("Day 1"),
			                     contest.getContestTitle());
			widget.refresh();
			QTableWidget *table = widget.findChild<QTableWidget *>();
			report << QStringLiteral("tab table: rows=%1 columns=%2")
			              .arg(table ? table->rowCount() : -1)
			              .arg(table ? table->columnCount() : -1);

			QStringList listed;

			for (int row = 0; table && row < table->rowCount(); ++row)
				listed << (table->item(row, 0) ? table->item(row, 0)->text() : QString());

			report << QStringLiteral("tab regions: %1").arg(listed.join(QStringLiteral(" | ")));

			if (! table || table->columnCount() != 4 || table->rowCount() < 1)
				fail(QStringLiteral("admission tab table"));

			if (table && table->contextMenuPolicy() != Qt::CustomContextMenu)
				fail(QStringLiteral("admission tab table has no context menu"));

			const QStringList tools = AdmissionGenerator::toolsReport().split(QStringLiteral(", "));
			report << QStringLiteral("tools: %1")
			              .arg(tools.isEmpty() ? QStringLiteral("ok") : tools.join(QStringLiteral(", ")));

			// 标题 / 测试时间：敲完没离开输入框（没有 editingFinished）也要存住 —— 直接关程序不能丢
			QLineEdit *titleEdit = widget.findChild<QLineEdit *>(QStringLiteral("titleEdit"));
			const QPushButton *venuesButton =
			    widget.findChild<QPushButton *>(QStringLiteral("venuesButton"));
			report << QStringLiteral("tab widgets: title=%1 venues=%2")
			              .arg(titleEdit ? 1 : 0)
			              .arg(venuesButton ? 1 : 0);

			if (! titleEdit || ! venuesButton)
				fail(QStringLiteral("the tab is missing the title box or the venues button"));

			if (titleEdit) {
				titleEdit->setText(QStringLiteral("day1"));
				QEventLoop loop;
				QTimer::singleShot(1500, &loop, &QEventLoop::quit);
				loop.exec();

				AdmissionProject saved;
				saved.load(true, nullptr);
				report << QStringLiteral("title after typing: %1").arg(saved.title);

				if (saved.title != QStringLiteral("day1"))
					fail(QStringLiteral("the title was not saved while typing"));
			}

			// 敲完立刻关窗口（连防抖都来不及跑）也不能丢：析构里还要再存一次
			{
				AdmissionWidget quick;
				quick.setContest(&contest);
				quick.setDayContext(QStringLiteral("day"), QStringLiteral("Day 1"),
				                    contest.getContestTitle());
				QLineEdit *quickTitle = quick.findChild<QLineEdit *>(QStringLiteral("titleEdit"));

				if (! quickTitle)
					fail(QStringLiteral("the quick widget has no title box"));
				else
					quickTitle->setText(QStringLiteral("closed right away"));
			}

			AdmissionProject afterClose;
			afterClose.load(true, nullptr);
			report << QStringLiteral("title after an immediate close: %1").arg(afterClose.title);

			if (afterClose.title != QStringLiteral("closed right away"))
				fail(QStringLiteral("closing the window dropped the title"));

			// 工作目录被别人改掉，也不许把标题写到别的目录去
			{
				const QString here = QDir::currentPath();
				QTemporaryDir other;

				if (other.isValid()) {
					AdmissionWidget pinned;
					pinned.setContest(&contest);
					pinned.setDayContext(QStringLiteral("day"), QStringLiteral("Day 1"),
					                     contest.getContestTitle());
					QDir::setCurrent(other.path());
					QLineEdit *pinnedTitle = pinned.findChild<QLineEdit *>(QStringLiteral("titleEdit"));

					if (! pinnedTitle)
						fail(QStringLiteral("the pinned widget has no title box"));
					else {
						pinnedTitle->setText(QStringLiteral("pinned title"));
						pinned.saveIfNeeded();
					}

					QDir::setCurrent(here);
					AdmissionProject pinnedCheck;
					pinnedCheck.load(true, nullptr);
					report << QStringLiteral("title with a changed cwd: %1").arg(pinnedCheck.title);

					if (pinnedCheck.title != QStringLiteral("pinned title"))
						fail(QStringLiteral("the title went to the wrong folder after the cwd changed"));

					if (QFileInfo::exists(other.path() + QStringLiteral("/admission")))
						fail(QStringLiteral("an admission/ folder was created outside the day"));
				}
			}

			// 生成日志：开始前要清空，而且要写到「谁 → 哪个文件」这一级
			QPlainTextEdit *buildLog = widget.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));

			if (! buildLog)
				fail(QStringLiteral("the build log box is missing"));
			else if (! AdmissionGenerator::toolsReport().isEmpty())
				report << QStringLiteral("build log: skipped (missing tools)");
			else {
				buildLog->setPlainText(QStringLiteral("stale line from the last run"));
				widget.build(QStringList{QStringLiteral("A赛区")});
				const QString text = buildLog->toPlainText();
				report << QStringLiteral("build log: %1")
				              .arg(text.split(QChar('\n')).join(QStringLiteral(" | ")));

				if (text.contains(QStringLiteral("stale line")))
					fail(QStringLiteral("the build log was not cleared"));

				if (! text.contains(QStringLiteral("张三")) || ! text.contains(QStringLiteral(".pdf")))
					fail(QStringLiteral("the build log says nothing about the tickets"));
			}
		}

		// 4) 名单编辑器（上半部分名单 + 下半部分赛区设置）：能读出刚才写的数据
		{
			AdmissionProject project;
			project.load(true, nullptr);
			AdmissionRegion *region = project.find(QStringLiteral("A赛区"));

			if (! region)
				fail(QStringLiteral("region missing for the editors"));
			else {
				CsvEditorDialog dialog(region->name, region->table, &project);
				QTableWidget *grid = dialog.findChild<QTableWidget *>(QStringLiteral("listGrid"));
				report << QStringLiteral("csv editor: rows=%1 columns=%2")
				              .arg(grid ? grid->rowCount() : -1)
				              .arg(grid ? grid->columnCount() : -1);

				if (! grid || grid->columnCount() != region->table.header.size() ||
				    grid->rowCount() != region->table.rows.size() || grid->rowCount() < 1)
					fail(QStringLiteral("csv editor did not load the list"));

				// 右键菜单必须挂在视图上：挂在 viewport 上会被滚动区域接管，菜单根本不会弹
				if (grid) {
					report << QStringLiteral("csv editor context menu: view=%1 viewport=%2")
					              .arg(grid->contextMenuPolicy() == Qt::CustomContextMenu
					                       ? QStringLiteral("custom")
					                       : QStringLiteral("no"))
					              .arg(grid->viewport()->contextMenuPolicy() == Qt::CustomContextMenu
					                       ? QStringLiteral("custom")
					                       : QStringLiteral("no"));

					if (grid->contextMenuPolicy() != Qt::CustomContextMenu)
						fail(QStringLiteral("csv editor grid has no context menu"));
				}

				QPlainTextEdit *notes = dialog.findChild<QPlainTextEdit *>(QStringLiteral("notesEdit"));
				const QPushButton *venues =
				    dialog.findChild<QPushButton *>(QStringLiteral("venuesButton"));
				report << QStringLiteral("list editor widgets: notes=%1 venues=%2")
				              .arg(notes ? 1 : 0)
				              .arg(venues ? 1 : 0);

				if (! venues)
					fail(QStringLiteral("the list editor has no venues button"));

				QStringList extra;

				for (const QString &name : region->table.header)
					if (! AdmissionTable::builtinColumns().contains(name))
						extra << name;

				report << QStringLiteral("list editor extras: %1")
				              .arg(extra.isEmpty() ? QStringLiteral("none") : extra.join(QStringLiteral(", ")));

				if (! extra.contains(QStringLiteral("考场位置")))
					fail(QStringLiteral("custom column lost while saving"));

				if (! notes || ! notes->toPlainText().contains(QStringLiteral("甲")))
					fail(QStringLiteral("notes box did not load the region notes"));

				NotesDialog notesDialog(QStringLiteral("Notes"), region->notes, nullptr);

				if (notesDialog.text() != region->notes)
					fail(QStringLiteral("notes dialog did not load the text"));

				// 撤销 / 重做：以前这里一点重做就闪退 —— 应用快照时整表重填会发 itemChanged，
				// 又被当成「用户改了格子」再 push 一步，撤销栈的 index 中途被改乱，
				// undo()/redo() 接着 at() 就越界了。
				{
					QAction *undoAction = nullptr;
					QAction *redoAction = nullptr;

					for (QAction *action : dialog.actions()) {
						if (action->shortcut() == QKeySequence(QKeySequence::Undo))
							undoAction = action;
						else if (action->shortcut() == QKeySequence(QKeySequence::Redo))
							redoAction = action;
					}

					if (! undoAction || ! redoAction)
						fail(QStringLiteral("undo/redo actions missing"));
					else if (undoAction->isEnabled())
						fail(QStringLiteral("the undo stack was touched while loading the grid"));
					else {
						grid->setItem(0, 0, new QTableWidgetItem(QStringLiteral("李四")));
						undoAction->trigger();
						const QString back = grid->item(0, 0) ? grid->item(0, 0)->text() : QString();
						redoAction->trigger();
						const QString forward =
						    grid->item(0, 0) ? grid->item(0, 0)->text() : QString();
						report << QStringLiteral("undo/redo: %1 -> %2").arg(back, forward);

						if (back != QStringLiteral("张三") || forward != QStringLiteral("李四"))
							fail(QStringLiteral("undo/redo did not restore the list"));
					}
				}
			}
		}

		// 4b) 注意事项 / 比赛注意：只认 [文字](链接)，其余全部当纯文本
		{
			const QString text = QStringLiteral("第一行 [官网](https://a.b/c_d#e)\n第二行 100% & A_B\n\n第三段");
			const QString latex = AdmissionNotes::toLatex(text);
			const QString markdown = AdmissionNotes::toLatex(QStringLiteral("**加粗** # 标题"));
			report << QStringLiteral("notes render: %1").arg(latex);

			if (! latex.contains(QStringLiteral("\\href{https://a.b/c\\_d\\#e}{官网}")) ||
			    ! latex.contains(QStringLiteral("100\\% \\& A\\_B")) ||
			    ! latex.contains(QStringLiteral("\\newline")) ||
			    ! latex.contains(QStringLiteral("\\par")) ||
			    ! markdown.contains(QStringLiteral("**加粗** \\# 标题")))
				fail(QStringLiteral("notes rendering: ") + latex + QStringLiteral(" / ") + markdown);
		}

		// 4c) 准考证号策略：默认规则能出号、全局序号跨赛区、对话框能原样带回策略
		{
			AdmissionProject project;
			project.load(true, nullptr);
			AdmissionRegion *item = project.find(QStringLiteral("A赛区"));
			AdmissionAssign::IdOptions options;
			options.templateText =
			    QStringLiteral("<section>-S<number><number><number><number><number>");
			QStringList ids;
			QString error;

			if (! item || ! AdmissionAssign::planIds(item->table, item->name, options, ids, &error))
				fail(QStringLiteral("planIds: ") + error);
			else {
				report << QStringLiteral("ticket numbers: %1").arg(ids.value(0));

				if (ids.value(0) != QStringLiteral("A赛区-S00001"))
					fail(QStringLiteral("default ticket number: ") + ids.value(0));
			}

			// 数字固定取自名单行号：模板里 <number> 的个数就是位数
			options.templateText = QStringLiteral("<number><number><number>");
			options.globalOffset = 41; // 行号跟它无关，应该完全忽略

			if (! item || ! AdmissionAssign::planIds(item->table, item->name, options, ids, &error))
				fail(QStringLiteral("planIds (row): ") + error);
			else if (ids.value(0) != QStringLiteral("001"))
				fail(QStringLiteral("the digits are not the row number: ") + ids.value(0));
			else
				report << QStringLiteral("row-numbered ticket: %1").arg(ids.value(0));

			if (item) {
				IdRuleDialog dialog(item->table, item->name, options);
				const AdmissionAssign::IdOptions back = dialog.options();
				report << QStringLiteral("id rule dialog: %1 (combos=%2)")
				              .arg(back.templateText)
				              .arg(dialog.findChildren<QComboBox *>().size());

				if (back.templateText != options.templateText ||
				    back.charSources != options.charSources)
					fail(QStringLiteral("the id rule dialog did not keep the strategy"));

				// 数字的取值来源已经强制成行号：不该再出现「数字取自」的下拉
				if (! dialog.findChildren<QComboBox *>().isEmpty())
					fail(QStringLiteral("the id rule dialog still asks where the digits come from"));

				// 预览必须放在只读、能滚的文本框里：人数一多标签就会把对话框撑爆
				auto *preview = dialog.findChild<QPlainTextEdit *>(QStringLiteral("previewText"));
				report << QStringLiteral("id rule preview: %1 lines=%2 readOnly=%3")
				              .arg(preview ? 1 : 0)
				              .arg(preview ? preview->toPlainText().count(QChar('\n')) + 1 : -1)
				              .arg(preview && preview->isReadOnly() ? 1 : 0);

				if (! preview || ! preview->isReadOnly() ||
				    preview->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff)
					fail(QStringLiteral("the id rule preview is not a read-only scroll area"));

				// 号码一律覆盖已有的：名单里原来有号也要重新编
				AdmissionTable existing = item->table;
				const int idColumn = existing.columnIndex(QStringLiteral("准考证号"));
				existing.rows[0][idColumn] = QStringLiteral("OLD-NUMBER");
				AdmissionAssign::IdOptions always = options;
				always.templateText = QStringLiteral("<number>");
				always.charSources = QStringList();

				if (! AdmissionAssign::planIds(existing, item->name, always, ids, &error) ||
				    ids.value(0) != QStringLiteral("1"))
					fail(QStringLiteral("existing ticket numbers are not overwritten"));
				else
					report << QStringLiteral("existing ticket number: OLD-NUMBER -> %1").arg(ids.value(0));
			}
		}

		// 4d) 排座位：考点 / 考场方案（容量）是唯一根据，两种摊法 + 座位号位数
		{
			AdmissionTable seats;
			seats.header = AdmissionTable::builtinColumns();

			for (int index = 0; index < 15; ++index)
				seats.rows << QStringList{QStringLiteral("选手%1").arg(index + 1), QString(), QString(),
				                          QString(), QString(), QString()};

			AdmissionVenue venue;
			venue.name = QStringLiteral("考点一");
			venue.rooms << AdmissionRoom{QStringLiteral("101"), 20}
			            << AdmissionRoom{QStringLiteral("102"), 20};
			QList<AdmissionVenue> venues{venue};

			AdmissionAssign::SeatOptions options;
			options.layout = AdmissionAssign::FillFirst;
			QStringList venueValues;
			QStringList roomValues;
			QStringList seatValues;
			QString seatError;

			if (! AdmissionAssign::planSeats(seats, venues, options, venueValues, roomValues, seatValues,
			                                 &seatError))
				fail(QStringLiteral("planSeats (fill first): ") + seatError);
			else {
				QHash<QString, int> filled;

				for (const QString &room : roomValues)
					filled[room] += 1;

				report << QStringLiteral("seats (fill first): 101=%1 102=%2 first=%3 last=%4")
				              .arg(filled.value(QStringLiteral("101")))
				              .arg(filled.value(QStringLiteral("102")))
				              .arg(seatValues.value(0), seatValues.value(14));

				if (filled.value(QStringLiteral("101")) != 15 || filled.value(QStringLiteral("102")) != 0 ||
				    seatValues.value(0) != QStringLiteral("01") ||
				    seatValues.value(14) != QStringLiteral("15") ||
				    venueValues.value(0) != QStringLiteral("考点一"))
					fail(QStringLiteral("fill-first seating"));
			}

			options.layout = AdmissionAssign::Balanced;

			if (! AdmissionAssign::planSeats(seats, venues, options, venueValues, roomValues, seatValues,
			                                 &seatError))
				fail(QStringLiteral("planSeats (balanced): ") + seatError);
			else {
				QHash<QString, int> filled;

				for (const QString &room : roomValues)
					filled[room] += 1;

				report << QStringLiteral("seats (balanced): 101=%1 102=%2 last101=%3 first102=%4")
				              .arg(filled.value(QStringLiteral("101")))
				              .arg(filled.value(QStringLiteral("102")))
				              .arg(seatValues.value(7), seatValues.value(8));

				// 15 人 2 个考场 → 8 / 7：第 8 行还是 101 的 8 号（最大人数 8 是一位数，不补零），
				// 第 9 行才是 102 的 1 号
				if (filled.value(QStringLiteral("101")) != 8 || filled.value(QStringLiteral("102")) != 7 ||
				    roomValues.value(8) != QStringLiteral("102") ||
				    seatValues.value(7) != QStringLiteral("8") || seatValues.value(8) != QStringLiteral("1"))
					fail(QStringLiteral("balanced seating"));
			}

			AdmissionVenue small;
			small.name = QStringLiteral("考点二");
			small.rooms << AdmissionRoom{QStringLiteral("201"), 4};

			if (AdmissionAssign::planSeats(seats, QList<AdmissionVenue>{small}, options, venueValues,
			                               roomValues, seatValues, &seatError))
				fail(QStringLiteral("seating should fail when the rooms are too small"));
			else
				report << QStringLiteral("seats (overflow): %1").arg(seatError);

			// 排座位的预览也必须是只读、能滚的文本框（考场一多标签就撑爆面板）
			options.layout = AdmissionAssign::FillFirst;
			SeatDialog seatDialog(seats, venues, options);
			auto *seatPreview = seatDialog.findChild<QPlainTextEdit *>(QStringLiteral("previewText"));
			report << QStringLiteral("seat preview: %1 lines=%2 readOnly=%3")
			              .arg(seatPreview ? 1 : 0)
			              .arg(seatPreview ? seatPreview->toPlainText().count(QChar('\n')) + 1 : -1)
			              .arg(seatPreview && seatPreview->isReadOnly() ? 1 : 0);

			if (! seatPreview || ! seatPreview->isReadOnly() ||
			    seatPreview->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff)
				fail(QStringLiteral("the seat preview is not a read-only scroll area"));

			// 顺序只允许「名单顺序 / 随机」：不该再有「按姓名」
			int seatCombos = 0;
			bool nameOrder = false;

			for (auto *box : seatDialog.findChildren<QComboBox *>()) {
				++seatCombos;

				if (box->findData(QStringLiteral("name")) >= 0)
					nameOrder = true;
			}

			report << QStringLiteral("seat dialog: combos=%1 byName=%2").arg(seatCombos).arg(nameOrder ? 1 : 0);

			if (nameOrder)
				fail(QStringLiteral("the seat dialog still offers sorting by name"));

			// 传个老配置里的 order=name 也不能改变结果：一律按名单行号
			QStringList rowVenues;
			QStringList rowRooms;
			QStringList rowSeats;
			options.order = QStringLiteral("row");

			if (! AdmissionAssign::planSeats(seats, venues, options, rowVenues, rowRooms, rowSeats, &seatError))
				fail(QStringLiteral("planSeats (row order): ") + seatError);
			else {
				options.order = QStringLiteral("name");
				QStringList nameVenues;
				QStringList nameRooms;
				QStringList nameSeats;

				if (! AdmissionAssign::planSeats(seats, venues, options, nameVenues, nameRooms, nameSeats,
				                                 &seatError))
					fail(QStringLiteral("planSeats (unknown order): ") + seatError);
				else if (nameSeats != rowSeats || nameRooms != rowRooms || nameVenues != rowVenues)
					fail(QStringLiteral("an unknown order should fall back to the list order"));
				else
					report << QStringLiteral("unknown seat order fell back to the list order");
			}
		}

		// 4e) 考点 / 考场编辑区：树建得起来也读得回来（考点只存名字，容量在考场那一行）
		{
			AdmissionProject project;
			project.load(true, nullptr);
			AdmissionRegion *item = project.find(QStringLiteral("A赛区"));

			if (! item)
				fail(QStringLiteral("the venue editor has no region to work on"));
			else {
				VenueDialog dialog(item->name, item->venues);
				const QList<AdmissionVenue> back = dialog.venues();
				report << QStringLiteral("venue editor: venues=%1 rooms=%2 capacity=%3")
				              .arg(back.size())
				              .arg(back.value(0).rooms.size() + back.value(1).rooms.size())
				              .arg(back.value(0).rooms.value(0).capacity + back.value(0).rooms.value(1).capacity +
				                   back.value(1).rooms.value(0).capacity);

				if (back.size() != 2 || back.value(0).rooms.size() != 2 || back.value(1).rooms.size() != 1 ||
				    back.value(0).name != QStringLiteral("考点一") ||
				    back.value(0).rooms.value(1).capacity != 2 ||
				    back.value(1).rooms.value(0).name != QStringLiteral("201"))
					fail(QStringLiteral("the venue editor did not keep the plan"));
			}
		}

		// 5) 删除赛区：整个赛区目录（名单 / 通告 / 列规则）一起没
		{
			AdmissionProject project;
			project.load(true, nullptr);
			QString error;
			const bool removed = project.removeRegion(QStringLiteral("A赛区"), &error);
			const bool gone = ! QDir(AdmissionProject::regionFolder(QStringLiteral("A赛区"))).exists();
			report << QStringLiteral("remove region: %1")
			              .arg(removed && gone ? QStringLiteral("ok") : QStringLiteral("FAILED"));

			if (! removed || ! gone)
				fail(QStringLiteral("removeRegion: ") + error);
		}

		report.prepend(QStringLiteral("problems=%1").arg(problems));

		QFile out(QDir(dayFolder).absoluteFilePath(reportFile));

		if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&out);
			stream << report.join(QChar('\n')) << '\n';
		}

		LOG("check-admission-ui: problems", problems);
		return problems == 0 ? 0 : 1;
	}

	// 隐藏入口：走一遍最真实的「写完就关程序」——真的建主窗口、真的打开比赛日、
	// 真的切到准考证页、真的 close()，再看磁盘上存没存住。
	// 用法：lemon.exe --check-close <比赛日.cdf> <报告文件>
	if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--check-close")) {
		QApplication app(argc, argv);
		Q_INIT_RESOURCE(resource);
		const QString dayFile = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
		const QString dayFolder = QFileInfo(dayFile).absolutePath();
		const QString configFile = dayFolder + QStringLiteral("/admission/config.json");
		const QString contestNotesFile = dayFolder + QStringLiteral("/admission/contest-notes.md");
		QStringList report;
		int problems = 0;
		const auto fail = [&](const QString &line) {
			problems++;
			report << (QStringLiteral("FAIL ") + line);
		};
		const auto savedTitle = [&configFile]() {
			QFile file(configFile);

			if (! file.open(QIODevice::ReadOnly))
				return QStringLiteral("<missing>");

			return QJsonDocument::fromJson(file.readAll())
			    .object()
			    .value(QStringLiteral("title"))
			    .toString();
		};
		const auto readFile = [](const QString &path) {
			QFile file(path);
			return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
		};
		const auto writeFile = [](const QString &path, const QByteArray &data) {
			QDir().mkpath(QFileInfo(path).absolutePath());
			QFile file(path);

			if (! file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return false;

			return file.write(data) == data.size();
		};

		// 先铺一份「用户原来的数据」：标题 + 比赛注意 + 一个赛区。
		// 打开比赛日不该把它冲掉 —— 这曾经是「重启以后标题没了」的真凶。
		writeFile(configFile,
		          QStringLiteral("{\n\t\"version\": \"1.0\",\n\t\"title\": \"pre-existing title\"\n}\n").toUtf8());
		writeFile(contestNotesFile, QStringLiteral("比赛注意内容\n").toUtf8());

		{
			LemonLime window;
			window.show();
			// .cdf 自己从 C++ 写（外部用 PowerShell 写的会带 UTF-8 BOM，正好拿来试 BOM 兼容）
			const QString checkDay = QFileInfo::exists(dayFile) ? dayFile
			                                                   : dayFolder + QStringLiteral("/check-day.cdf");

			if (checkDay != dayFile)
				writeFile(checkDay, QStringLiteral("{\"version\":\"1.0\",\"contestTitle\":\"CheckDay\","
				                                   "\"regionEnabled\":true}").toUtf8());

			const bool opened = window.openContestForCheck(checkDay);
			report << QStringLiteral("opened=%1 title=%2 cwd=%3")
			              .arg(opened ? 1 : 0)
			              .arg(window.windowTitle(), QDir::currentPath());

			auto *tabs = window.findChild<QTabWidget *>();
			auto *tab = window.findChild<QWidget *>(QStringLiteral("admissionTab"));
			auto *admission = window.findChild<AdmissionWidget *>();
			QLineEdit *title =
			    admission ? admission->findChild<QLineEdit *>(QStringLiteral("titleEdit")) : nullptr;
			report << QStringLiteral("window: tabs=%1 tab=%2 admission=%3 title=%4")
			              .arg(tabs ? 1 : 0)
			              .arg(tab ? 1 : 0)
			              .arg(admission ? 1 : 0)
			              .arg(title ? 1 : 0);

			if (! tabs || ! tab || ! admission || ! title)
				fail(QStringLiteral("the main window has no admission tab"));
			else {
				report << QStringLiteral("cwd=%1 root=%2 config=%3 window=\"%4\" modal=%5")
				              .arg(QDir::currentPath(), AdmissionProject::root())
				              .arg(QFileInfo::exists(configFile) ? 1 : 0)
				              .arg(window.windowTitle())
				              .arg(QApplication::activeModalWidget() ? QStringLiteral("yes")
				                                                     : QStringLiteral("no"));
				tabs->setCurrentWidget(tab);
				QCoreApplication::processEvents();
				report << QStringLiteral("title loaded from disk: \"%1\" / notes: \"%2\"")
				              .arg(title->text(), readFile(contestNotesFile).trimmed());

				// 打开比赛日就把盘上的设置清了（combo 填条目触发的首次写盘）—— 必须不能发生
				if (title->text() != QStringLiteral("pre-existing title"))
					fail(QStringLiteral("opening the day wiped the saved title"));

				if (readFile(configFile).trimmed().isEmpty())
					fail(QStringLiteral("opening the day wiped config.json"));

				if (readFile(contestNotesFile).trimmed() != QStringLiteral("比赛注意内容"))
					fail(QStringLiteral("opening the day wiped contest-notes.md"));

				// 敲完立刻关窗口：连 400ms 的防抖都来不及跑
				title->setText(QStringLiteral("closed right away"));
				window.close();
				report << QStringLiteral("saved right after close(): \"%1\" (via %2)")
				              .arg(savedTitle(), AdmissionProject::configPath());
				report << QStringLiteral("config.json now: %1")
				              .arg(QString(readFile(configFile)).split(QChar('\n')).join(QStringLiteral(" ")));
			}
		}

		const QString finalTitle = savedTitle();
		report << QStringLiteral("title after the window was gone: \"%1\"").arg(finalTitle);

		if (finalTitle != QStringLiteral("closed right away"))
			fail(QStringLiteral("closing the program dropped the title"));

		report.prepend(QStringLiteral("problems=%1").arg(problems));
		QFile out(QString::fromLocal8Bit(argv[3]));

		if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&out);
			stream << report.join(QChar('\n')) << '\n';
		}

		LOG("check-close: problems", problems);
		return problems == 0 ? 0 : 1;
	}

	// 隐藏入口：发布包自检（不需要任何比赛数据），给打包脚本用。
	// 用法：lemon.exe --self-test <报告文件>
	//   检查发布目录里该有的东西：Qt 插件（平台 / SVG 图标）、题面模板、内嵌翻译、
	//   并真的把主窗口建一遍（菜单 / 工具条上的 SVG 图标全都要经 iconengines/qsvgicon 加载）。
	//   全部正常返回 0，否则返回 1（报告里写着 FAIL 的行）。
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

		// SVG 图标：图片插件（imageformats/qsvg）与图标引擎（iconengines/qsvgicon）缺一不可。
		QStringList formats;

		for (const QByteArray &format : QImageReader::supportedImageFormats())
			formats << QString::fromLatin1(format);

		report << QStringLiteral("image formats=%1").arg(formats.join(QChar(',')));

		if (! formats.contains(QStringLiteral("svg")))
			fail(QStringLiteral("imageformats/qsvg.dll 缺失，读不了 SVG"));

		const QStringList icons{QStringLiteral(":/icon/icon.png"), QStringLiteral(":/icon/acrobat.svg"),
		                        QStringLiteral(":/icon/document-save.svg"),
		                        QStringLiteral(":/icon/view-refresh.svg"),
		                        QStringLiteral(":/logo/splash2.png")};
		int loaded = 0;

		for (const QString &path : icons)
			if (! QIcon(path).pixmap(16, 16).isNull())
				loaded++;

		report << QStringLiteral("icons=%1/%2").arg(loaded).arg(icons.size());

		if (loaded != icons.size())
			fail(QStringLiteral("有图标加载不出来（iconengines/qsvgicon.dll 或资源缺失）"));

		// 题面模板：PDF 导出要用，发布时必须放在 lemon.exe 旁边。
		const QString templates = StatementBuilder::templateRoot();
		report << QStringLiteral("statement templates=%1").arg(templates);

		if (templates.isEmpty())
			fail(QStringLiteral("找不到题面模板 statement-templates/"));

		// 内嵌翻译（LEMON_EMBED_TRANSLATIONS）。
		const QStringList languages = QDir(QStringLiteral(":/translation/")).entryList({QStringLiteral("*.qm")});
		report << QStringLiteral("embedded translations=%1").arg(languages.join(QChar(',')));

		if (languages.isEmpty())
			fail(QStringLiteral("没有内嵌翻译"));

		// 主窗口：菜单 / 工具条 / 各种对话框上的图标都在这时候加载。
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
	// fonts.setFamily("Noto Sans CJK SC");
#endif
#ifdef Q_OS_WIN32
	QFont fonts;
	fonts.setFamily("Microsoft YaHei");
	fonts.setHintingPreference(QFont::PreferNoHinting);
	SingleApplication::setFont(fonts);
#endif
#ifdef Q_OS_MAC
	// fonts.setFamily("PingFangSC-Regular");
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
