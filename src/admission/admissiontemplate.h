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

/**
 * 内置的准考证模板（唯一，不提供给用户编辑）。
 *
 * 模板放在 `assets/templates/admission/main.tex`（发布时在 lemon.exe 旁边的 `templates/admission/`）。
 * 表格里有 6 行锁定区，由程序按名单填充；其余位置用 `{{...}}` 占位符替换。
 */
namespace AdmissionTemplate {
	/// 模板目录；找不到时返回空串。
	QString root();
	QString filePath();
	QString source();
	/// 随模板一起发布的字体目录（沙箱里会拷成 ./fonts/）。
	QString fontDir();
	/// 锁定区那 6 行的标签（姓名 / 准考证号 / 测试时间 / 考点 / 考场 / 座位号）。
	QStringList lockedRowLabels();
	/// 校验锁定区结构：6 行、且模板里有全部占位符。
	bool validate(const QString &text, QString *error);
	/// 把 {{key}} 换成值；没换掉的占位符收进 unresolved。
	QString render(const QString &text, const QMap<QString, QString> &values, QStringList *unresolved);
} // namespace AdmissionTemplate
