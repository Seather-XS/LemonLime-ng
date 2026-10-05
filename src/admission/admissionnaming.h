/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QMap>
#include <QString>
#include <QStringList>

/** 命名 / 列模板渲染时的上下文。 */
struct AdmissionNamingContext {
	QString section;   ///< 赛区名
	QString name;      ///< 姓名
	QString id;        ///< 准考证号
	QString seat;      ///< 座位号
	QString room;      ///< 考场
	int row{0};        ///< CSV 行号（从 1）
	int col{0};        ///< 列号（生成列时用，从 1）
	int regionSeq{0};  ///< 赛区内序号（从 1）
	int globalSeq{0};  ///< 全局序号（从 1）
	QMap<QString, QString> columns; ///< 自定义列的当前值
};

/**
 * 占位符模板：`<section> <number> <char> <row> <col> <name> <id> <seat> <room>` 与 `<自定义列>`。
 *
 * 连续同类占位符算一组（组内共享一个值，个数就是位数）：`<number><number>` 是两位，
 * `<char><char>` 是两位字母。数字组、字母组各最多 2 组，组间不互相依赖。
 */
namespace AdmissionNaming {
	/// 保留字（自定义列不能与它们同名）。
	QStringList reservedKeywords();
	/// 数一下某类占位符分成了几组。
	int groupCount(const QString &pattern, const QString &keyword);
	/// 模板合法性：尖括号闭合、组数不超限、引用名是保留字或可用列。
	bool validate(const QString &pattern, const QStringList &columns, QString *error);
	/// 渲染。numberSources / charSources 按组给来源
	/// （regionSeq / globalSeq / row / col / seat / id / name / room / column:<列名>）；
	/// 给少了就按设计默认值 regionSeq 兜底。
	QString render(const QString &pattern, const AdmissionNamingContext &context,
	               const QStringList &numberSources, const QStringList &charSources, QString *error);
	/// 界面上用来解释模板的一句话。
	QString describe(const QString &pattern);
	/// 文件名安全化（按设计里的「非法字符 → 替换 / 跳过 / 报错」里的替换策略）。
	QString sanitize(const QString &name, const QString &replacement, int maxLength);
	/// Excel 风格字母：1→A、26→Z、27→AA、702→ZZ。
	QString excelLetters(int value);
} // namespace AdmissionNaming
