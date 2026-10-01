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

	/// 新建比赛日时给的一套默认限制。
	static QVector<ViolationRule> defaultRules();

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
