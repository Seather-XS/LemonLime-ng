/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QString>

/**
 * 注意事项 / 比赛注意 / 额外行单元格的排版。
 *
 * 只认 Markdown 的**超链接**语法 `[显示文字](链接)`，其余内容一律当纯文本：
 * `**加粗**`、`# 标题`、`- 列表` 这些都会原样印出来（不再走 pandoc）。
 */
namespace AdmissionNotes {
	/// LaTeX 特殊字符转义（换行折成空格）。
	QString escape(const QString &text);
	/// 纯文本 + 超链接 → LaTeX 片段：换行用 `\newline`、空行用 `\par`（都能放进 p{} 单元格）。
	QString toLatex(const QString &text);
} // namespace AdmissionNotes
