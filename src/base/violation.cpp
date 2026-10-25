/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "violation.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {
	const QString KIND_COMMAND = Violation::kindCommand();
	const QString KIND_STRING = Violation::kindString();

	const QStringList MODIFIERS = {QStringLiteral("volatile"), QStringLiteral("__volatile__"),
	                               QStringLiteral("goto"), QStringLiteral("inline"),
	                               QStringLiteral("__inline__")};

	QString commandName(const QString &text) {
		QString name = text.trimmed();

		while (name.endsWith(QLatin1Char(')'))) {
			name.chop(1);
			name = name.trimmed();
		}

		if (name.endsWith(QLatin1Char('('))) {
			name.chop(1);
			name = name.trimmed();
		}

		return name;
	}

	QRegularExpression commandPattern(const QString &text) {
		const QString name = commandName(text);

		if (name.isEmpty())
			return QRegularExpression();

		if (name.startsWith(QLatin1Char('#')))
			return QRegularExpression(QStringLiteral("^[ \\t]*") + QRegularExpression::escape(name) +
			                              QStringLiteral("\\b"),
			                          QRegularExpression::MultilineOption);

		QStringList mods;

		for (const QString &item : MODIFIERS)
			mods.append(QRegularExpression::escape(item));

		const QString pattern = QStringLiteral("(?<![.>\\w])") + QRegularExpression::escape(name) +
		                        QStringLiteral("\\b\\s*(?:") + mods.join(QLatin1Char('|')) +
		                        QStringLiteral("\\s*)?\\(");
		return QRegularExpression(pattern);
	}

	int lineOf(const QString &text, int position) {
		return text.left(position).count(QLatin1Char('\n')) + 1;
	}

	QString snippetOf(const QStringList &lines, int line) {
		QString snippet;

		if (line >= 1 && line <= lines.size())
			snippet = lines.at(line - 1).trimmed();

		if (snippet.size() > 120)
			snippet = snippet.left(120) + QStringLiteral("…");

		return snippet;
	}

	QVector<ViolationRule> cppCommands() {
		return {
		    {KIND_COMMAND, QStringLiteral("system")},
		    {KIND_COMMAND, QStringLiteral("popen")},
		    {KIND_COMMAND, QStringLiteral("fork")},
		    {KIND_COMMAND, QStringLiteral("exec")},
		    {KIND_COMMAND, QStringLiteral("execl")},
		    {KIND_COMMAND, QStringLiteral("execv")},
		    {KIND_COMMAND, QStringLiteral("execve")},
		    {KIND_COMMAND, QStringLiteral("CreateProcess")},
		    {KIND_COMMAND, QStringLiteral("WinExec")},
		    {KIND_COMMAND, QStringLiteral("ShellExecute")},
		    {KIND_COMMAND, QStringLiteral("CreateThread")},
		    {KIND_COMMAND, QStringLiteral("_beginthread")},
		    {KIND_COMMAND, QStringLiteral("_beginthreadex")},
		    {KIND_COMMAND, QStringLiteral("pthread_create")},
		    {KIND_COMMAND, QStringLiteral("socket")},
		    {KIND_COMMAND, QStringLiteral("WSAStartup")},
		    {KIND_COMMAND, QStringLiteral("gethostbyname")},
		    {KIND_COMMAND, QStringLiteral("#pragma")},
		    {KIND_COMMAND, QStringLiteral("__asm__")},
		    {KIND_COMMAND, QStringLiteral("asm")},
		    {KIND_STRING, QStringLiteral("windows.h")},
		    {KIND_STRING, QStringLiteral("unistd.h")},
		    {KIND_STRING, QStringLiteral("sys/socket.h")},
		    {KIND_STRING, QStringLiteral("winsock2.h")},
		};
	}

	QVector<ViolationRule> pascalCommands() {
		return {
		    {KIND_COMMAND, QStringLiteral("ExecuteProcess")},
		    {KIND_COMMAND, QStringLiteral("fpSystem")},
		    {KIND_COMMAND, QStringLiteral("fpExec")},
		    {KIND_COMMAND, QStringLiteral("fpExecv")},
		    {KIND_COMMAND, QStringLiteral("fpExecve")},
		    {KIND_COMMAND, QStringLiteral("fpFork")},
		    {KIND_COMMAND, QStringLiteral("CreateProcess")},
		    {KIND_COMMAND, QStringLiteral("WinExec")},
		    {KIND_COMMAND, QStringLiteral("ShellExecute")},
		    {KIND_COMMAND, QStringLiteral("CreateThread")},
		    {KIND_COMMAND, QStringLiteral("BeginThread")},
		    {KIND_STRING, QStringLiteral("process")},
		    {KIND_STRING, QStringLiteral("BaseUnix")},
		    {KIND_STRING, QStringLiteral("Sockets")},
		    {KIND_STRING, QStringLiteral("cthreads")},
		    {KIND_STRING, QStringLiteral("uses windows")},
		};
	}

	QVector<ViolationRule> freeBasicCommands() {
		return {
		    {KIND_COMMAND, QStringLiteral("Exec")},
		    {KIND_COMMAND, QStringLiteral("Chain")},
		    {KIND_COMMAND, QStringLiteral("CreateProcess")},
		    {KIND_COMMAND, QStringLiteral("WinExec")},
		    {KIND_COMMAND, QStringLiteral("ShellExecute")},
		    {KIND_COMMAND, QStringLiteral("CreateThread")},
		    {KIND_STRING, QStringLiteral("open pipe")},
		    {KIND_STRING, QStringLiteral("windows.bi")},
		};
	}

	QVector<ViolationRule> javaCommands() {
		return {
		    {KIND_COMMAND, QStringLiteral("exec")},
		    {KIND_STRING, QStringLiteral("ProcessBuilder")},
		    {KIND_STRING, QStringLiteral("Runtime.getRuntime")},
		    {KIND_STRING, QStringLiteral("java.net")},
		    {KIND_STRING, QStringLiteral("javax.net")},
		    {KIND_STRING, QStringLiteral("java.lang.Thread")},
		    {KIND_STRING, QStringLiteral("Thread.start")},
		};
	}

	QVector<ViolationRule> pythonDefaultRules() {
		return {
		    {KIND_STRING, QStringLiteral("os.system")},
		    {KIND_STRING, QStringLiteral("os.popen")},
		    {KIND_STRING, QStringLiteral("os.fork")},
		    {KIND_STRING, QStringLiteral("os.exec")},
		    {KIND_STRING, QStringLiteral("os.spawn")},
		    {KIND_STRING, QStringLiteral("os.posix_spawn")},
		    {KIND_STRING, QStringLiteral("subprocess")},
		    {KIND_COMMAND, QStringLiteral("eval")},
		    {KIND_COMMAND, QStringLiteral("exec")},
		    {KIND_COMMAND, QStringLiteral("compile")},
		    {KIND_COMMAND, QStringLiteral("__import__")},
		    {KIND_STRING, QStringLiteral("threading")},
		    {KIND_STRING, QStringLiteral("_thread")},
		    {KIND_STRING, QStringLiteral("multiprocessing")},
		    {KIND_STRING, QStringLiteral("socket")},
		    {KIND_STRING, QStringLiteral("urllib")},
		    {KIND_STRING, QStringLiteral("http.client")},
		    {KIND_STRING, QStringLiteral("requests")},
		    {KIND_STRING, QStringLiteral("ctypes")},
		    {KIND_STRING, QStringLiteral("cffi")},
		};
	}
} 

auto Violation::defaultRules() -> QVector<ViolationRule> { return cppCommands(); }

auto Violation::languages() -> const QVector<ViolationLanguage> & {
	static const QVector<ViolationLanguage> list = {
	    {QStringLiteral("C/C++"),
	     {QStringLiteral("gcc"), QStringLiteral("g++"), QStringLiteral("cc"), QStringLiteral("c++"),
	      QStringLiteral("clang"), QStringLiteral("clang++"), QStringLiteral("cl")},
	     {QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"), QStringLiteral("cxx"),
	      QStringLiteral("c++"), QStringLiteral("h"), QStringLiteral("hpp")},
	     cppCommands()},
	    {QStringLiteral("Free Pascal"),
	     {QStringLiteral("fpc"), QStringLiteral("ppc386"), QStringLiteral("ppcx64")},
	     {QStringLiteral("pas"), QStringLiteral("pp"), QStringLiteral("inc")},
	     pascalCommands()},
	    {QStringLiteral("FreeBASIC"), {QStringLiteral("fbc")}, {QStringLiteral("bas")}, freeBasicCommands()},
	    {QStringLiteral("Java"), {QStringLiteral("jdk"), QStringLiteral("javac"), QStringLiteral("java")},
	     {QStringLiteral("java")}, javaCommands()},
	    {QStringLiteral("Python"), {QStringLiteral("python"), QStringLiteral("pypy")},
	     {QStringLiteral("py"), QStringLiteral("pyw")}, pythonDefaultRules()},
	};
	return list;
}

auto Violation::languageFor(const QString &compilerName, const QStringList &sourceExtensions)
    -> const ViolationLanguage * {
	const QString name = compilerName.trimmed().toLower();

	if (! name.isEmpty()) {
		for (const ViolationLanguage &language : languages())
			for (const QString &alias : language.compilers)
				if (name == alias || name.contains(alias))
					return &language;
	}

	for (const ViolationLanguage &language : languages())
		for (const QString &extension : sourceExtensions) {
			const QString normalized = extension.trimmed().toLower();

			if (! normalized.isEmpty() && language.extensions.contains(normalized))
				return &language;
		}

	return nullptr;
}

auto Violation::languageNameFor(const QString &compilerName, const QStringList &sourceExtensions) -> QString {
	const ViolationLanguage *language = languageFor(compilerName, sourceExtensions);
	return language ? language->name : QString();
}

auto Violation::defaultRulesFor(const QString &compilerName, const QStringList &sourceExtensions)
    -> QVector<ViolationRule> {
	const ViolationLanguage *language = languageFor(compilerName, sourceExtensions);

	return language ? language->rules : defaultRules();
}

auto Violation::defaultRulesFor(const QStringList &sourceExtensions) -> QVector<ViolationRule> {
	return defaultRulesFor(QString(), sourceExtensions);
}

auto Violation::normalizeRules(const QVector<ViolationRule> &rules) -> QVector<ViolationRule> {
	QVector<ViolationRule> result;
	QSet<QString> seen;

	for (const ViolationRule &entry : rules) {
		QString kind = entry.kind.trimmed().toLower();
		const QString text = entry.text.trimmed();

		if (kind.isEmpty())
			kind = KIND_COMMAND;

		if (kind != KIND_COMMAND && kind != KIND_STRING)
			continue;

		if (text.isEmpty())
			continue;

		const QString key = kind + QLatin1Char('\n') + text;

		if (seen.contains(key))
			continue;

		seen.insert(key);
		result.append({kind, text});
	}

	return result;
}

auto Violation::maskCode(const QString &text) -> QString {
	QString out;
	out.reserve(text.size());
	const int size = text.size();
	int index = 0;

	while (index < size) {
		const QChar ch = text.at(index);
		const QChar nxt = index + 1 < size ? text.at(index + 1) : QChar();

		if (ch == QLatin1Char('/') && nxt == QLatin1Char('/')) { 
			while (index < size && text.at(index) != QLatin1Char('\n')) {
				out.append(QLatin1Char(' '));
				index += 1;
			}

			continue;
		}

		if (ch == QLatin1Char('/') && nxt == QLatin1Char('*')) { 
			out.append(QStringLiteral("  "));
			index += 2;

			while (index < size &&
			       ! (text.at(index) == QLatin1Char('*') && index + 1 < size &&
			          text.at(index + 1) == QLatin1Char('/'))) {
				out.append(text.at(index) == QLatin1Char('\n') ? QLatin1Char('\n') : QLatin1Char(' '));
				index += 1;
			}

			if (index < size) { 
				out.append(QStringLiteral("  "));
				index += 2;
			}

			continue;
		}

		if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) { 
			const QChar quote = ch;
			out.append(QLatin1Char(' '));
			index += 1;

			while (index < size) {
				const QChar current = text.at(index);

				if (current == QLatin1Char('\\') && index + 1 < size) { 
					out.append(QStringLiteral("  "));
					index += 2;
					continue;
				}

				out.append(current == QLatin1Char('\n') ? QLatin1Char('\n') : QLatin1Char(' '));
				index += 1;

				if (current == quote)
					break;
			}

			continue;
		}

		out.append(ch);
		index += 1;
	}

	return out;
}

auto Violation::checkSource(const QString &text, const QVector<ViolationRule> &rules) -> QVector<Match> {
	const QString source = text;
	const QString masked = maskCode(source);
	const QStringList lines = source.split(QLatin1Char('\n'));
	const QString lowered = source.toLower();
	QVector<Match> found;

	for (const ViolationRule &rule : normalizeRules(rules)) {
		int position = -1;

		if (rule.kind == KIND_COMMAND) {
			const QRegularExpression re = commandPattern(rule.text);

			if (! re.isValid())
				continue;

			const auto hit = re.match(masked);

			if (hit.hasMatch())
				position = hit.capturedStart();
		} else {
			position = lowered.indexOf(rule.text.toLower());

			if (position < 0)
				position = -1;
		}

		if (position < 0)
			continue;

		const int line = lineOf(source, position);
		found.append({rule, line, snippetOf(lines, line)});
	}

	return found;
}

bool Violation::checkFiles(const QStringList &paths, const QVector<ViolationRule> &rules, QString *outPath,
                           QVector<Match> *outMatches) {
	const QVector<ViolationRule> normalized = normalizeRules(rules);

	if (normalized.isEmpty())
		return false;

	for (const QString &path : paths) {
		QFile file(path);

		if (! file.open(QIODevice::ReadOnly | QIODevice::Text))
			continue;

		const QString text = QString::fromUtf8(file.readAll());
		file.close();
		const QVector<Match> found = checkSource(text, normalized);

		if (! found.isEmpty()) {
			if (outPath)
				*outPath = path;

			if (outMatches)
				*outMatches = found;

			return true;
		}
	}

	return false;
}

auto Violation::describe(const QString &path, const QVector<Match> &matches) -> QString {
	QStringList parts;

	for (const Match &item : matches) {
		parts.append(QStringLiteral("第 %1 行命中%2「%3」：%4")
		                 .arg(item.line)
		                 .arg(item.rule.isCommand() ? QStringLiteral("命令") : QStringLiteral("字符串"))
		                 .arg(item.rule.text)
		                 .arg(item.snippet.isEmpty() ? QStringLiteral("（该行只有空白）") : item.snippet));
	}

	return QStringLiteral("%1 %2").arg(QFileInfo(path).fileName(), parts.join(QStringLiteral("；")));
}

auto Violation::shortText(const QString &path, const QVector<Match> &matches) -> QString {
	if (matches.isEmpty())
		return QStringLiteral("违规");

	const Match &first = matches.first();
	return QStringLiteral("违规（%1「%2」，%3 第 %4 行%5）")
	    .arg(first.rule.isCommand() ? QStringLiteral("命令") : QStringLiteral("字符串"))
	    .arg(first.rule.text)
	    .arg(QFileInfo(path).fileName())
	    .arg(first.line)
	    .arg(matches.size() > 1 ? QStringLiteral("，共 %1 条").arg(matches.size()) : QString());
}
