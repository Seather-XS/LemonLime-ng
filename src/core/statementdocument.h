/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

/**
 * 一道试题的题面块。
 *
 * 与 statement-editor 一致：块的首行是标题 `# 中文名（英文名）`，其余行是正文。
 */
class StatementProblem {
  public:
	QString title;     ///< 中文名
	QString english;   ///< 英文名（可为空）
	QStringList body;  ///< 正文（不含标题行）

	/// `# 中文名（英文名）` / `# 中文名` / `#`
	QString heading() const;
};

/**
 * 题面 markdown 文档模型，结构与工作区里的 statement-editor 完全相同：
 *
 * - 可选的 YAML front matter（`title` / `subtitle` / `day` / `date`）
 * - 四个区段 `<!-- SECTION: name --> ... <!-- END: name -->`
 * - `<!-- PROBLEM -->` 分隔的试题块（兼容旧的按 `# 标题` 直接分块写法）
 *
 * 未被编辑的内容（注释、其它区段、试题区之前的正文）都会原样保留。
 */
class StatementDocument {
  public:
	static const QStringList &knownSections();
	static const QStringList &editableMetaKeys();
	static QString problemMarker();

	/// 判断一行是否是试题标题（`# 中文名（英文名）`），是则拆出中英文名。
	static bool headingOf(const QString &line, QString &title, QString &english);

	static StatementDocument parse(const QString &text);

	/// 序列化回 markdown（与 statement-editor 的写出格式一致）。
	QString toMarkdown() const;

	bool hasFrontMatter() const { return hasFront; }
	/// `title` / `subtitle` / `day` / `date`
	QString metadata(const QString &key) const;
	void setMetadata(const QString &key, const QString &value);

	/// 文档中真实出现过的区段名（小写，按出现顺序）。
	QStringList sectionNames() const;
	bool hasSection(const QString &name) const;
	QString sectionBody(const QString &name) const;
	void setSectionBody(const QString &name, const QString &body);

	/// 试题区之前的正文（只读保留）。
	QString headText() const;

	QList<StatementProblem> problems() const;
	void setProblems(const QList<StatementProblem> &problems);

  private:
	struct Item {
		bool isSection{false};
		QString text;  ///< isSection 为 false 时的正文
		QString name;  ///< isSection 为 true 时的区段名（小写）
		QString body;  ///< isSection 为 true 时的区段内容
	};

	static QStringList splitLines(const QString &text);
	static bool isProblemMarker(const QString &line);
	/// 返回 false 表示这一行不是试题标题。
	static bool matchProblemHeading(const QString &line, QString &title, QString &english);
	static QString problemTitleText(const QString &line);

	static void splitProblemArea(const QString &text, QStringList &head, QList<StatementProblem> &problems);
	static QString serializeProblemArea(const QStringList &head, const QList<StatementProblem> &problems);

	/// 试题区所在正文段的序号（-1 表示文档里还没有试题）。
	int problemsItem() const;
	/// 试题区的 markdown 正文。
	QString problemsText() const;

	bool hasFront{false};
	QList<QPair<QString, QString>> metaEntries;  ///< 键、原始行（键为空表示原样保留的行）
	QMap<QString, QString> metaValues;           ///< title / subtitle / day / date 的当前值
	QList<Item> items;
};
