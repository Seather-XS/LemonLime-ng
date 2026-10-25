/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

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
