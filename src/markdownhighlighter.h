/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

/**
 * 简易 Markdown 高亮，规则与 statement-editor 相同：
 *
 * - 标题行整行加粗
 * - `**加重**` 加粗、`*斜体*` 斜体
 * - 行内数学 `$…$` 紫色
 * - 行内代码 `` `…` `` 黄色
 * - 代码围栏 ``` 内的内容不做高亮
 */
class MarkdownHighlighter : public QSyntaxHighlighter {
	Q_OBJECT

  public:
	explicit MarkdownHighlighter(QTextDocument *parent = nullptr);

  protected:
	void highlightBlock(const QString &text) override;

  private:
	QTextCharFormat headingFormat;
	QTextCharFormat boldFormat;
	QTextCharFormat italicFormat;
	QTextCharFormat mathFormat;
	QTextCharFormat codeFormat;
	QTextCharFormat fenceFormat;
};
