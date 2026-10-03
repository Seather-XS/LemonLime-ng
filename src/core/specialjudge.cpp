/*
 * SPDX-FileCopyrightText: 2026 gengen-tuack
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "specialjudge.h"
#include "base/ProcessUtil.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>

namespace {

// 自定义校验器只支持 C++ 源码（.cpp）：评测前编译一次，之后复用编译结果。
// 现成的可执行文件、其它后缀的源码都不再接受。
const QString CHECKER_SUFFIX("cpp");
const QString TESTLIB_RESOURCE(":/testlib/testlib.h");
const QString TESTLIB_HEADER("testlib.h");

QMutex cacheMutex;
QHash<QString, QString> cache;
QString workDir;
bool postRoutineAdded = false;

bool isCppChecker(const QString &path) {
	return QFileInfo(path).suffix().compare(CHECKER_SUFFIX, Qt::CaseInsensitive) == 0;
}

bool includesTestlib(const QString &path) {
	QFile file(path);

	if (! file.open(QFile::ReadOnly | QFile::Text))
		return false;

	QString text = QString::fromUtf8(file.readAll());
	text.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));
	text.remove(QRegularExpression(R"(//[^\n]*)"));

	return text.contains(R"(#include "testlib.h")") || text.contains(R"(#include <testlib.h>)") ||
	       text.contains(R"(#include "testlib_for_lemons.h")") ||
	       text.contains(R"(#include <testlib_for_lemons.h>)");
}

QString ensureWorkDir() {
	if (workDir.isEmpty()) {
		workDir = QDir::tempPath() + QDir::separator() +
		          QString("lemonlime-specialjudge-%1").arg(QCoreApplication::applicationPid());
	}

	QDir().mkpath(workDir);
	return workDir;
}

QString ensureTestlibIncludeDir() {
	QString dir = ensureWorkDir() + QDir::separator() + "include";
	QDir().mkpath(dir);
	QString header = dir + QDir::separator() + TESTLIB_HEADER;

	if (! QFile::exists(header)) {
		if (! QFile::copy(TESTLIB_RESOURCE, header))
			return QString();
	}

	return dir;
}

QString compileChecker(const QString &source, const QString &compilerLocation, const QString &compileTemplate,
                       int timeLimitMs, QString &error) {
	QString base = QFileInfo(source).completeBaseName();
	QString suffix = QFileInfo(source).suffix();
	QCryptographicHash hash(QCryptographicHash::Sha1);
	hash.addData(source.toUtf8());
	QString dir = ensureWorkDir() + QDir::separator() +
	              QString("build-%1").arg(QString::fromLatin1(hash.result().toHex().left(12)));
	QDir().mkpath(dir);

	QString sourceName = base + "." + suffix;
	QString sourcePath = dir + QDir::separator() + sourceName;
	QFile::remove(sourcePath);

	if (! QFile::copy(source, sourcePath)) {
		error = QObject::tr("Cannot copy the special judge.");
		return QString();
	}

	QStringList arguments = compileTemplate.split(QLatin1Char(' '), Qt::SkipEmptyParts);

	for (auto &argument : arguments) {
		argument.replace("%s.*", sourceName);
		argument.replace("%s", base);
	}

	// A checker may ship its own testlib.h, which takes precedence over the bundled one.
	QString includeDir = QFileInfo(source).absolutePath();

	if (! QFile::exists(includeDir + QDir::separator() + TESTLIB_HEADER))
		includeDir = ensureTestlibIncludeDir();

	if (includeDir.isEmpty()) {
		error = QObject::tr("Cannot find the testlib.h for the special judge.");
		return QString();
	}

	arguments.append("-I" + QDir::toNativeSeparators(includeDir));

	QProcess compilerProcess;
	Lemon::common::suppressConsoleWindow(compilerProcess);
	compilerProcess.setProcessChannelMode(QProcess::MergedChannels);
	compilerProcess.setWorkingDirectory(QDir::toNativeSeparators(dir));
	compilerProcess.start(compilerLocation, arguments);

	if (! compilerProcess.waitForStarted(-1)) {
		error = QObject::tr("Cannot start the compiler.");
		return QString();
	}

	QElapsedTimer timer;
	timer.start();
	bool finished = false;

	while (timer.elapsed() < timeLimitMs) {
		if (compilerProcess.waitForFinished(10)) {
			finished = true;
			break;
		}

		QCoreApplication::processEvents();
	}

	if (! finished) {
		compilerProcess.kill();
		error = QObject::tr("Compiling the special judge timed out.");
		return QString();
	}

	if (compilerProcess.exitCode() != 0) {
		error = QString::fromLocal8Bit(compilerProcess.readAllStandardOutput().constData());

		if (error.trimmed().isEmpty())
			error = QObject::tr("Compiling the special judge failed.");

		return QString();
	}

	QString executable = dir + QDir::separator() + base;
#ifdef Q_OS_WIN32
	executable.append(".exe");
#endif

	if (! QFile::exists(executable)) {
		error = QObject::tr("The compiled special judge was not found.");
		return QString();
	}

	return QDir::toNativeSeparators(executable);
}

void addPostRoutine() {
	if (postRoutineAdded)
		return;

	postRoutineAdded = true;
	qAddPostRoutine(SpecialJudge::cleanup);
}

} // namespace

auto SpecialJudge::resolve(const QString &checkerPath, const QString &compilerLocation,
                           const QString &compileTemplate, int timeLimitMs, QString &error) -> QString {
	static bool resourcesInitialized = false;

	if (! resourcesInitialized) {
		Q_INIT_RESOURCE(testlib);
		resourcesInitialized = true;
	}

	error.clear();

	QFileInfo info(checkerPath);

	if (! info.exists()) {
		error = QObject::tr("Cannot find the special judge.");
		return QString();
	}

	if (! isCppChecker(checkerPath)) {
		error = QObject::tr("The special judge must be a .cpp source file.");
		return QString();
	}

	if (! includesTestlib(checkerPath)) {
		error = QObject::tr("The special judge must include \"testlib.h\".");
		return QString();
	}

	if (compilerLocation.isEmpty()) {
		error = QObject::tr("No compiler is available to build the special judge.");
		return QString();
	}

	QString key = QString("%1|%2|%3|%4|%5")
	                  .arg(info.absoluteFilePath())
	                  .arg(info.lastModified().toMSecsSinceEpoch())
	                  .arg(info.size())
	                  .arg(compilerLocation)
	                  .arg(compileTemplate);

	QMutexLocker locker(&cacheMutex);
	auto cached = cache.constFind(key);

	if (cached != cache.constEnd()) {
		if (QFile::exists(cached.value())) {
			addPostRoutine();
			return cached.value();
		}

		cache.erase(cached);
	}

	QString executable = compileChecker(info.absoluteFilePath(), compilerLocation, compileTemplate, timeLimitMs, error);

	if (executable.isEmpty())
		return QString();

	cache.insert(key, executable);
	addPostRoutine();
	return executable;
}

void SpecialJudge::cleanup() {
	QMutexLocker locker(&cacheMutex);
	cache.clear();

	if (! workDir.isEmpty())
		QDir(workDir).removeRecursively();

	workDir.clear();
}
