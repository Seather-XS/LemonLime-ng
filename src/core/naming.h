/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include <QString>

/**
 * 选手文件夹命名限制：选手代码文件夹必须按格式串命名，否则不测试、全部 0 分、试题标紫。
 *
 * 格式串里支持的参数（其余字符按字面量处理，整名**完全匹配**）：
 * - `<section>`：必须与该选手所在赛区名相同；
 * - `<number>`：任意 0~9 的数字；
 * - `<char>`：任意大写、小写字母。
 *
 * 例：`<section>-S<number><number><number><number><number>`。
 *
 * 语义与 gengen-tuack 一致。
 */
class Naming {
  public:
	/// 新建比赛日给的默认格式。
	static QString defaultPattern();

	/// 格式串里的全部参数名。
	static QStringList parameters();

	/// 格式串 → 正则体（`<section>` 换成该赛区名，其它参数换成字符类，其余字符转义）。
	static QString toRegexBody(const QString &pattern, const QString &section);

	/// 文件夹名是否符合限制（整名完全匹配）；格式串为空时一律算符合。
	static bool matches(const QString &name, const QString &section, const QString &pattern);

	/// 把参数换成示例字：`<section>-S<number>` → `HN-Sx`。
	static QString sample(const QString &pattern, const QString &section);

	/// 写进结果的说明文字。
	static QString describe(const QString &name, const QString &section, const QString &pattern);

	/// 取路径的文件夹名。
	static QString folderName(const QString &path);
};
