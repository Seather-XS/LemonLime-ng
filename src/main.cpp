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
#include "core/statementbuilder.h"
#include "spdlog/sinks/daily_file_sink.h"
//
#include <QApplication>
#include <QDebug>
#include <QPixmap>
#include <QSplashScreen>
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
