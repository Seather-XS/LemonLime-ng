// 临时小工具：直接调用 Settings::ensureTaskDirs() / ensureDistDirs()，验证：
//   * 不再建题目下的 tests/，并清掉老的 tests/；
//   * 建好 dist/reports、dist/export，并清掉根目录下老的 reports/、export/。
// 用法：ensure_dirs_test.exe <比赛日目录> <题目名>
#include "base/LemonLog.hpp"
#include "base/settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QLibraryInfo>
#include <QPluginLoader>

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);

	if (argc < 3) {
		qWarning() << "usage: ensure_dirs_test <dayDir> <taskName>";
		return 1;
	}

	// 真实的初始化在 main.cpp 里做；这里只装一个最简 logger，否则 LOG() 会打到空指针上。
	Lemon::base::logger = std::make_shared<spdlog::logger>("ensure-dirs-test");

	QDir::setCurrent(QString::fromLocal8Bit(argv[1]));
	Settings::ensureTaskDirs(QString::fromLocal8Bit(argv[2]));
	Settings::ensureDistDirs();
	qInfo() << "ensureTaskDirs/ensureDistDirs done for" << QString::fromLocal8Bit(argv[2]);
	return 0;
}
