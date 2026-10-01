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
#include "base/settings.h"
#include "component/exportutil/exportutil.h"
#include "core/contest.h"
#include "core/packagebuilder.h"
#include "core/statementbuilder.h"
#include "resultviewer.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "statisticsbrowser.h"
//
#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QSplashScreen>
#include <QTextStream>
#include <chrono>

#define LEMON_MODULE_NAME "Main"

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

	// 隐藏入口：命令行把比赛日打包成 .zip（与界面「导出」选项卡同一段逻辑）。
	// 用法：lemon.exe --export-package <比赛日.cdf> [--kind contestant|testdata] [--wrap] [--nested]
	//        [--no-per-task] [--no-structure] [--samples] [--password <密码>]
	//   生成 <比赛日目录>/export/<比赛日名>.zip
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
