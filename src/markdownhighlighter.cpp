/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "markdownhighlighter.h"
//
#include <QRegularExpression>
#include <QTextDocument>

namespace {
	/// 行内代码：`code`
	const QRegularExpression &codeSpanPattern() {
		static const QRegularExpression re(QStringLiteral(R"(`[^`\n]*`)"));
		return re;
	}

	/// 行内数学：$...$
	const QRegularExpression &mathPattern() {
		static const QRegularExpression re(QStringLiteral(R"((?<!\\)\$(?!\$)[^$\n]+(?<!\\)\$(?!\$))"));
		return re;
	}

	const QRegularExpression &boldPattern() {
		static const QRegularExpression re(QStringLiteral(R"(\*\*[^*\n]+\*\*)"));
		return re;
	}

	const QRegularExpression &italicPattern() {
		static const QRegularExpression re(QStringLiteral(R"((?<![*\w])\*[^*\n]+\*(?![*\w]))"));
		return re;
	}

	const QRegularExpression &headingPattern() {
		static const QRegularExpression re(QStringLiteral(R"(^\s{0,3}#{1,6}\s)"));
		return re;
	}
} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *parent) : QSyntaxHighlighter(parent) {
	headingFormat.setFontWeight(QFont::Bold);
	headingFormat.setForeground(QColor(0x1f, 0x4f, 0x8f));

	boldFormat.setFontWeight(QFont::Bold);

	italicFormat.setFontItalic(true);

	mathFormat.setForeground(QColor(0x8a, 0x2b, 0xe2));

	// 行内代码：只改字色，不加背景色（背景色会把行内代码所在的整段/表格格子涂满，很难看）。
	// statement-editor 的编辑器是深色底，所以那边用黄色字色；这里是浅色底，用深棕字。
	codeFormat.setForeground(QColor(0x7a, 0x50, 0x00));

	fenceFormat.setForeground(QColor(0x80, 0x80, 0x80));
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
	// 围栏状态用 0（正常）/ 1（围栏内）表示；从上一块继承。
	const int fenceState = previousBlockState() == 1 ? 1 : 0;

	if (text.trimmed().startsWith(QStringLiteral("```"))) {
		const int next = fenceState == 1 ? 0 : 1;
		setCurrentBlockState(next);
		setFormat(0, text.length(), fenceFormat);
		return;
	}

	if (fenceState == 1) {
		setCurrentBlockState(1);
		setFormat(0, text.length(), fenceFormat);
		return;
	}

	setCurrentBlockState(0);

	// 超长块直接不管，避免粘贴大段内容时卡顿
	if (text.length() > 4000)
		return;

	if (headingPattern().match(text).hasMatch()) {
		setFormat(0, text.length(), headingFormat);
		return;
	}

	// 先记下所有行内代码的范围，数学/加重/斜体不在这范围内匹配。
	QList<QPair<int, int>> codeRanges;
	QRegularExpressionMatchIterator codeIt = codeSpanPattern().globalMatch(text);

	while (codeIt.hasNext()) {
		const QRegularExpressionMatch matched = codeIt.next();
		codeRanges.append({matched.capturedStart(), matched.capturedEnd()});
		setFormat(matched.capturedStart(), matched.capturedLength(), codeFormat);
	}

	auto inCode = [&codeRanges](int position) {
		for (const auto &range : codeRanges)
			if (position >= range.first && position < range.second)
				return true;

		return false;
	};

	auto applyOutside = [&](const QRegularExpression &pattern, const QTextCharFormat &format) {
		QRegularExpressionMatchIterator it = pattern.globalMatch(text);

		while (it.hasNext()) {
			const QRegularExpressionMatch matched = it.next();

			if (inCode(matched.capturedStart()))
				continue;

			setFormat(matched.capturedStart(), matched.capturedLength(), format);
		}
	};

	applyOutside(mathPattern(), mathFormat);
	applyOutside(italicPattern(), italicFormat);
	applyOutside(boldPattern(), boldFormat);
}
