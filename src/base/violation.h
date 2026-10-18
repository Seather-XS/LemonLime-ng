/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * 违规检测：检查选手代码里有没有违禁的「命令」或「字符串」，命中就整位选手不算数。
 *
 * 规则按语言（编译器）分别保存：Compiler 里存一份 ViolationRule 列表，比赛日只决定
 * 「启用哪些语言的规则」（见 Contest::isViolationCompilerEnabled）。
 *
 * 语义与 gengen-tuack 一致：
 * - **命令**（kind == "command"）：出现这个字符串不一定算违规，只有「真的用到」（标识符后面
 *   跟着 `(`，且不是 `obj.` / `ptr->` 的成员访问）才算；以 `#` 开头（如 `#pragma`）的按行首的
 *   预处理指令匹配。匹配前会先把注释与字符串字面量挖掉。
 * - **字符串**（kind == "string"）：出现就算，不区分大小写，注释里出现也算。
 *
 * 这里只做纯逻辑，界面与引擎都从这里取结论。
 */
struct ViolationRule {
	QString kind; // "command" 或 "string"
	QString text;

	bool isCommand() const { return kind == QStringLiteral("command"); }
};

/**
 * 一个语言（编译器）的内置限制清单。
 *
 * 「命令类限制」就是这份清单里的命令条目：程序按编译器名 / 源文件后缀找到对应语言，
 * 再从它的清单里读取默认规则（新建编译器、读取旧设置、点「恢复默认」时都走这里），
 * 而不是把某一门语言的清单硬套给所有编译器。
 */
struct ViolationLanguage {
	QString name;           ///< 显示用的语言名，如 "C/C++"。
	QStringList compilers;  ///< 编译器名（小写），如 gcc / g++ / clang。
	QStringList extensions; ///< 源文件后缀（小写），如 cpp / py。
	QVector<ViolationRule> rules;
};

class Violation {
  public:
	/// 一条命中的记录（line 从 1 开始）。
	struct Match {
		ViolationRule rule;
		int line = 0;
		QString snippet;
	};

	static QString kindCommand() { return QStringLiteral("command"); }
	static QString kindString() { return QStringLiteral("string"); }

	/// C/C++ 口径的默认限制（没有匹配到具体语言时的兜底）。
	static QVector<ViolationRule> defaultRules();

	/// 内置支持的语言清单：C/C++、Free Pascal、FreeBASIC、Java、Python。
	static const QVector<ViolationLanguage> &languages();

	/// 按编译器名（先）与源文件后缀（后）找到语言；都匹配不上返回 nullptr。
	static const ViolationLanguage *languageFor(const QString &compilerName,
	                                            const QStringList &sourceExtensions);

	/// 这个编译器对应的语言名；认不出来时为空。
	static QString languageNameFor(const QString &compilerName, const QStringList &sourceExtensions);

	/// 从编译器所属语言的清单里读取默认限制；认不出语言时退回 defaultRules()。
	static QVector<ViolationRule> defaultRulesFor(const QString &compilerName,
	                                              const QStringList &sourceExtensions);

	/// 只知道后缀时的简化版（认不出语言时退回 defaultRules()）。
	static QVector<ViolationRule> defaultRulesFor(const QStringList &sourceExtensions);

	/// 整理规则表：丢掉空内容与认不出的类型，顺序保持原样。
	static QVector<ViolationRule> normalizeRules(const QVector<ViolationRule> &rules);

	/// 把注释与字符串/字符字面量换成空格（长度与换行都不变），行号因此仍然准。
	static QString maskCode(const QString &text);

	/// 在源码里找违规：每条规则最多报一次（第一次命中）。
	static QVector<Match> checkSource(const QString &text, const QVector<ViolationRule> &rules);

	/// 逐个源文件检查，返回第一个有违规的文件；都没问题返回 false。
	static bool checkFiles(const QStringList &paths, const QVector<ViolationRule> &rules, QString *outPath,
	                       QVector<Match> *outMatches);

	/// 拼成写进结果的说明文字。
	static QString describe(const QString &path, const QVector<Match> &matches);

	/// 日志里那一行用的短说明。
	static QString shortText(const QString &path, const QVector<Match> &matches);
};
