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
#include "core/statementbuilder.h"
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
#include <QIcon>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSplashScreen>
#include <QTextStream>
#include <QTimer>
#include <chrono>

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
