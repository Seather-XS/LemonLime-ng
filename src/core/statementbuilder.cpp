/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/statementbuilder.h"
//
#include "base/LemonLog.hpp"
#include "base/ProcessUtil.hpp"
//
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVector>

#include <functional>

#define LEMON_MODULE_NAME "StatementBuild"

namespace {
	// ==================================================================================
	// 与 statement-editor 一致的常量
	// ==================================================================================

	const QString &tildeToken() {
		static const QString token = QStringLiteral("@@TILDE@@");
		return token;
	}

	const QString &metaSentinel() {
		static const QString sentinel = QStringLiteral("QQMETAQQ");
		return sentinel;
	}

	/// 区段名 → 生成 LaTeX 里使用的占位标记。
	const QMap<QString, QString> &userSectionMarkers() {
		static const QMap<QString, QString> markers = {
		    {QStringLiteral("overview_table"), QStringLiteral("USEROVERVIEWTABLE")},
		    {QStringLiteral("notice"), QStringLiteral("USERNOTICE")},
		    {QStringLiteral("submission_filename"), QStringLiteral("USERSUBMISSIONFILENAMETABLE")},
		    {QStringLiteral("compile_options"), QStringLiteral("USERCOMPILEOPTIONSTABLE")},
		};
		return markers;
	}

	const QStringList &noiOnlySections() {
		static const QStringList list = {QStringLiteral("submission_filename"), QStringLiteral("compile_options")};
		return list;
	}

	const QStringList &ccpcSkippedSections() {
		static const QStringList list = {QStringLiteral("overview_table"), QStringLiteral("submission_filename"),
		                                 QStringLiteral("compile_options"), QStringLiteral("notice")};
		return list;
	}

	bool isNoiTemplate(const QString &name) {
		return name == QStringLiteral("noi") || name == QStringLiteral("noi new");
	}

	/// 代码高亮相关的导言区补丁（与 statement-editor 的 `_HIGHLIGHT_PREAMBLE` 相同）。
	const char *highlightPreamble() {
		return R"STMT(
\usepackage{fancyvrb}
\usepackage{framed}
\usepackage{longtable}
\providecommand{\passthrough}[1]{#1}
\providecommand{\tightlist}{\setlength{\itemsep}{0pt}\setlength{\parskip}{0pt}}
\newenvironment{statementtable}[2][3]{%
  \def\statementtablecols{#1}%
  \renewcommand{\arraystretch}{1.1}%
  \setlength{\arrayrulewidth}{0.4pt}%
  \begin{tabular}{@{}c*{\numexpr#1-1\relax}{|c}@{}}%
  \noalign{\global\arrayrulewidth=2pt}\cline{1-#1}\noalign{\global\arrayrulewidth=0.4pt}%
  #2\\\noalign{\global\arrayrulewidth=1pt}\cline{1-#1}\noalign{\global\arrayrulewidth=0.4pt}%
}{%
  \noalign{\global\arrayrulewidth=2pt}\cline{1-\statementtablecols}\end{tabular}%
}
\newsavebox{\statementtablebox}
% 表格上下留白：在模板里用 \renewcommand 就能调；导览区（概览表 / 提交源程序文件名 /
% 编译选项）用后一个长度，通常要写得更紧凑一些。
\providecommand{\statementtableskip}{\bigskipamount}
\providecommand{\statementoverviewtableskip}{0.6em}
)STMT";
	}

	// ==================================================================================
	// 通用小工具
	// ==================================================================================

	const QRegularExpression &problemMarkerLinePattern() {
		static const QRegularExpression re(QStringLiteral(R"(^[ \t]*<!--\s*PROBLEM\s*-->[ \t]*$)"),
		                                   QRegularExpression::CaseInsensitiveOption |
		                                       QRegularExpression::MultilineOption);
		return re;
	}

	QString texEscape(const QString &value) {
		static const QMap<QChar, QString> replacements = {
		    {QChar('\\'), QStringLiteral("\\textbackslash{}")}, {QChar('&'), QStringLiteral("\\&")},
		    {QChar('%'), QStringLiteral("\\%")},                {QChar('$'), QStringLiteral("\\$")},
		    {QChar('#'), QStringLiteral("\\#")},                {QChar('_'), QStringLiteral("\\_")},
		    {QChar('{'), QStringLiteral("\\{")},                {QChar('}'), QStringLiteral("\\}")},
		    {QChar('~'), QStringLiteral("$\\sim$")},            {QChar('^'), QStringLiteral("\\textasciicircum{}")},
		};
		QString result;
		result.reserve(value.length());

		for (const QChar &ch : value)
			result += replacements.value(ch, QString(ch));

		return result;
	}

	/// Python `re.split` 会保留捕获组，Qt 的 split() 不会，这里补上。
	QStringList splitKeepingCaptures(const QString &text, const QRegularExpression &re) {
		QStringList parts;
		int position = 0;
		QRegularExpressionMatchIterator it = re.globalMatch(text);

		while (it.hasNext()) {
			const QRegularExpressionMatch matched = it.next();
			parts.append(text.mid(position, matched.capturedStart() - position));

			for (int i = 1; i <= matched.lastCapturedIndex(); i++)
				parts.append(matched.captured(i));

			position = matched.capturedEnd();
		}

		parts.append(text.mid(position));
		return parts;
	}

	QString maskTildes(const QString &text) {
		if (! text.contains(QChar('~')))
			return text;

		static const QRegularExpression codeSpan(QStringLiteral(R"(`[^`\n]*`)"));
		const QStringList lines = text.split(QChar('\n'));
		QStringList out;
		bool inFence = false;

		for (const QString &line : lines) {
			if (line.trimmed().startsWith(QStringLiteral("```"))) {
				inFence = ! inFence;
				out.append(line);
				continue;
			}

			if (inFence || ! line.contains(QChar('~'))) {
				out.append(line);
				continue;
			}

			QStringList pieces;
			int position = 0;
			QRegularExpressionMatchIterator it = codeSpan.globalMatch(line);

			while (it.hasNext()) {
				const QRegularExpressionMatch matched = it.next();
				QString head = line.mid(position, matched.capturedStart() - position);
				head.replace(QChar('~'), tildeToken());
				pieces.append(head);
				pieces.append(matched.captured(0));
				position = matched.capturedEnd();
			}

			QString tail = line.mid(position);
			tail.replace(QChar('~'), tildeToken());
			pieces.append(tail);
			out.append(pieces.join(QString()));
		}

		return out.join(QChar('\n'));
	}

	QString fixInlineMath(const QRegularExpressionMatch &matched) {
		const QString body = matched.captured(QStringLiteral("body")).trimmed();

		if (body.isEmpty())
			return matched.captured(0);

		static const QRegularExpression cjk(QStringLiteral("[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]"));

		if (! body.contains(QChar('\\')) && cjk.match(body).hasMatch())
			return matched.captured(0);

		QString fixed = QStringLiteral("$") + QString(body).replace(QChar('~'), QStringLiteral("\\sim")) +
		                QStringLiteral("$");

		if (! matched.captured(QStringLiteral("digit")).isEmpty())
			fixed += QStringLiteral("\\hspace{0pt}") + matched.captured(QStringLiteral("digit"));

		return fixed;
	}

	QString replaceInlineMath(const QString &text) {
		static const QRegularExpression mathInline(
		    QStringLiteral(R"((?<!\\)\$(?!\$)(?<body>[^$\n]*?)(?<!\\)\$(?!\$)(?<digit>\d?))"));
		QString result;
		int cursor = 0;
		QRegularExpressionMatchIterator it = mathInline.globalMatch(text);

		while (it.hasNext()) {
			const QRegularExpressionMatch matched = it.next();
			result += text.mid(cursor, matched.capturedStart() - cursor);
			result += fixInlineMath(matched);
			cursor = matched.capturedEnd();
		}

		result += text.mid(cursor);
		return result;
	}

	QString normalizeMathInLine(const QString &line) {
		if (! line.contains(QChar('$')))
			return line;

		static const QRegularExpression codeSpan(QStringLiteral(R"(`[^`\n]*`)"));
		QStringList pieces;
		int position = 0;
		QRegularExpressionMatchIterator it = codeSpan.globalMatch(line);

		while (it.hasNext()) {
			const QRegularExpressionMatch matched = it.next();
			pieces.append(replaceInlineMath(line.mid(position, matched.capturedStart() - position)));
			pieces.append(matched.captured(0));
			position = matched.capturedEnd();
		}

		pieces.append(replaceInlineMath(line.mid(position)));
		return pieces.join(QString());
	}

	QString normalizeInlineMath(const QString &text) {
		if (! text.contains(QChar('$')))
			return text;

		const QStringList lines = text.split(QChar('\n'));
		QStringList out;
		bool inFence = false;

		for (const QString &line : lines) {
			if (line.trimmed().startsWith(QStringLiteral("```"))) {
				inFence = ! inFence;
				out.append(line);
				continue;
			}

			out.append(inFence ? line : normalizeMathInLine(line));
		}

		return out.join(QChar('\n'));
	}

	const QRegularExpression &atxHeadingPattern() {
		static const QRegularExpression re(QStringLiteral(R"(^#{1,6}(?:[ \t]|$))"));
		return re;
	}

	const QRegularExpression &listItemPattern() {
		static const QRegularExpression re(QStringLiteral(R"(^(?P<indent>[ \t]*)(?:[-*+]|\d{1,9}[.)])(?:[ \t]|$)"));
		return re;
	}

	bool needsBlankBefore(const QString &line, const QString *prev, bool listContext) {
		if (prev == nullptr || prev->trimmed().isEmpty())
			return false;

		const bool indented = ! line.isEmpty() && (line.at(0) == QChar(' ') || line.at(0) == QChar('\t'));

		if (indented && listContext)
			return false;

		if (line.trimmed().startsWith(QStringLiteral("```")))
			return true;

		static const QRegularExpression tableRow(QStringLiteral(R"(^[ \t]*\|)"));
		static const QRegularExpression quote(QStringLiteral(R"(^[ \t]*>)"));

		if (tableRow.match(line).hasMatch())
			return ! tableRow.match(*prev).hasMatch();

		if (quote.match(line).hasMatch())
			return ! quote.match(*prev).hasMatch();

		if (atxHeadingPattern().match(line).hasMatch())
			return true;

		if (listItemPattern().match(line).hasMatch()) {
			if (listItemPattern().match(*prev).hasMatch())
				return false;

			if (! prev->isEmpty() && (prev->at(0) == QChar(' ') || prev->at(0) == QChar('\t')) && listContext)
				return false;

			return true;
		}

		return false;
	}

	QString normalizeBlockSeparation(const QString &text) {
		const QStringList lines = text.split(QChar('\n'));
		QStringList out;
		bool inFence = false;
		bool listContext = false;

		for (const QString &line : lines) {
			const QString stripped = line.trimmed();
			const bool isFenceMarker = stripped.startsWith(QStringLiteral("```"));

			if (inFence) {
				out.append(line);

				if (isFenceMarker)
					inFence = false;

				continue;
			}

			const QString *prev = out.isEmpty() ? nullptr : &out.last();

			if (needsBlankBefore(line, prev, listContext))
				out.append(QString());

			out.append(line);

			if (isFenceMarker) {
				inFence = true;
				listContext = false;
			} else if (stripped.isEmpty()) {
				listContext = false;
			} else if (listItemPattern().match(line).hasMatch()) {
				listContext = true;
			} else if (! (! line.isEmpty() && (line.at(0) == QChar(' ') || line.at(0) == QChar('\t')) &&
			             listContext)) {
				listContext = false;
			}
		}

		return out.join(QChar('\n'));
	}

	QString replaceProblemMarkers(const QString &text, bool &hasMarkers) {
		hasMarkers = problemMarkerLinePattern().match(text).hasMatch();

		if (! hasMarkers)
			return text;

		QString result = text;
		result.replace(problemMarkerLinePattern(), QStringLiteral("\\newpage"));
		return result;
	}

	// ==================================================================================
	// LaTeX 收尾处理（sanitize_latex）
	// ==================================================================================

	const QRegularExpression &quoteEnvPattern() {
		static const QRegularExpression re(
		    QStringLiteral(R"((\\begin\{verbatim\}.*?\\end\{verbatim\}|\\begin\{lstlisting\}.*?\\end\{lstlisting\}|\\begin\{Highlighting\}.*?\\end\{Highlighting\}))"),
		    QRegularExpression::DotMatchesEverythingOption);
		return re;
	}

	QString restoreCjkQuotes(const QString &text) {
		auto mapSegment = [](QString segment) {
			segment.replace(QStringLiteral("``"), QStringLiteral("\u201c"));
			segment.replace(QStringLiteral("''"), QStringLiteral("\u201d"));
			return segment;
		};

		QStringList parts;
		int last = 0;
		QRegularExpressionMatchIterator it = quoteEnvPattern().globalMatch(text);

		while (it.hasNext()) {
			const QRegularExpressionMatch matched = it.next();
			parts.append(mapSegment(text.mid(last, matched.capturedStart() - last)));
			parts.append(matched.captured(0));
			last = matched.capturedEnd();
		}

		parts.append(mapSegment(text.mid(last)));
		return parts.join(QString());
	}

	const QRegularExpression &passthroughInlinePattern() {
		static const QRegularExpression re(
		    QStringLiteral(R"(\\passthrough\{\\lstinline(?<opts>(?:\[[^\]]*\])?)(?<delim>.)(?<body>.*?)\k<delim>\})"),
		    QRegularExpression::DotMatchesEverythingOption);
		return re;
	}

	QString toTextttBody(const QString &body) {
		static const QMap<QChar, QString> escapes = {{QChar('\\'), QStringLiteral("\\textbackslash{}")},
		                                              {QChar('~'), QStringLiteral("\\textasciitilde{}")},
		                                              {QChar('^'), QStringLiteral("\\^{}")}};
		const QString kept = QStringLiteral("{}_%&#$");
		QString out;
		int index = 0;

		while (index < body.length()) {
			const QChar ch = body.at(index);

			if (ch == QChar('\\') && index + 1 < body.length()) {
				const QChar following = body.at(index + 1);

				if (escapes.contains(following)) {
					out += escapes.value(following);
					index += 2;
					continue;
				}

				if (kept.contains(following)) {
					out += ch;
					out += following;
					index += 2;
					continue;
				}
			} else if (ch == QChar('$')) {
				out += QStringLiteral("\\$");
				index++;
				continue;
			}

			out += ch;
			index++;
		}

		return out;
	}

	QString unwrapPassthroughInline(const QString &text) {
		QString result;
		int position = 0;
		QRegularExpressionMatchIterator it = passthroughInlinePattern().globalMatch(text);

		while (it.hasNext()) {
			const QRegularExpressionMatch matched = it.next();
			result += text.mid(position, matched.capturedStart() - position);
			result += QStringLiteral("\\texttt{") + toTextttBody(matched.captured(QStringLiteral("body"))) +
			          QStringLiteral("}");
			position = matched.capturedEnd();
		}

		result += text.mid(position);
		return result;
	}

	QStringList splitLatexRow(const QString &row) {
		static const QRegularExpression separator(QStringLiteral(R"((?<!\\)&)"));
		return row.split(separator);
	}

	QString shiftRowCells(const QString &row, const QString &offset) {
		const QStringList cells = splitLatexRow(row);
		QStringList shifted;

		for (const QString &cell : cells)
			shifted.append(QStringLiteral("\\raisebox{%1\\baselineskip}{%2}").arg(offset, cell.trimmed()));

		return shifted.join(QStringLiteral(" & "));
	}

	bool isMergeMarker(const QString &cell, const QString &marker) {
		const QString normalized = cell.trimmed();

		if (marker == QStringLiteral("up"))
			return normalized == QStringLiteral("\\^{}") || normalized == QStringLiteral("^");

		return normalized == QStringLiteral("\\textless{}") || normalized == QStringLiteral("<");
	}

	/// 与 statement-editor 的 `_render_merged_rows` 相同，返回渲染后的行，列数与分隔符。
	QStringList renderMergedRows(const QStringList &rows, int &columnCount, QStringList &separators) {
		QVector<QStringList> matrix;

		for (const QString &row : rows)
			matrix.append(splitLatexRow(row));

		columnCount = 0;

		for (QStringList &row : matrix)
			columnCount = qMax(columnCount, row.size());

		for (QStringList &row : matrix)
			while (row.size() < columnCount)
				row.append(QString());

		QStringList renderedRows;
		QVector<QSet<int>> blockedBoundaries(matrix.size());

		for (int rowIndex = 0; rowIndex < matrix.size(); rowIndex++) {
			const QStringList &row = matrix.at(rowIndex);
			QStringList cells;
			int columnIndex = 0;

			while (columnIndex < columnCount) {
				QString cell = row.at(columnIndex).trimmed();

				if (isMergeMarker(cell, QStringLiteral("up"))) {
					cells.append(QString());
					columnIndex++;
					continue;
				}

				if (isMergeMarker(cell, QStringLiteral("left"))) {
					columnIndex++;
					continue;
				}

				int columnSpan = 1;

				while (columnIndex + columnSpan < columnCount &&
				       isMergeMarker(row.at(columnIndex + columnSpan), QStringLiteral("left")))
					columnSpan++;

				int rowSpan = 1;

				while (rowIndex + rowSpan < matrix.size() &&
				       isMergeMarker(matrix.at(rowIndex + rowSpan).at(columnIndex), QStringLiteral("up")))
					rowSpan++;

				if (rowSpan > 1) {
					cell = QStringLiteral("\\multirow[c]{%1}{*}{\\raisebox{-0.12\\baselineskip}{%2}}")
					           .arg(rowSpan)
					           .arg(cell);

					for (int boundaryIndex = rowIndex; boundaryIndex < rowIndex + rowSpan - 1; boundaryIndex++)
						for (int column = columnIndex; column < columnIndex + columnSpan; column++)
							blockedBoundaries[boundaryIndex].insert(column);
				}

				if (columnSpan > 1)
					cell = QStringLiteral("\\multicolumn{%1}{|c|}{%2}").arg(columnSpan).arg(cell);

				cells.append(cell);
				columnIndex += columnSpan;
			}

			renderedRows.append(cells.join(QStringLiteral(" & ")));
		}

		for (const QSet<int> &blocked : blockedBoundaries) {
			if (blocked.isEmpty()) {
				separators.append(QStringLiteral("\\hline"));
				continue;
			}

			QStringList ranges;
			int start = -1;

			for (int columnIndex = 0; columnIndex <= columnCount; columnIndex++) {
				if (columnIndex < columnCount && ! blocked.contains(columnIndex)) {
					if (start < 0)
						start = columnIndex + 1;
				} else if (start >= 0) {
					ranges.append(QStringLiteral("\\cline{%1-%2}").arg(start).arg(columnIndex));
					start = -1;
				}
			}

			separators.append(ranges.join(QStringLiteral(" ")));
		}

		return renderedRows;
	}

	QString stripMinipage(const QString &text) {
		if (! text.contains(QStringLiteral("minipage")))
			return text;

		static const QRegularExpression minipageBegin(
		    QStringLiteral(R"(\\begin\{minipage\}(?:\[[^\]]*\])?\{[^{}]*\})"));
		static const QRegularExpression centeringLine(QStringLiteral(R"((?m)^[ \t]*\\centering[ \t]*\n?)"));
		QString result = text;
		result.remove(minipageBegin);
		result.remove(QStringLiteral("\\end{minipage}"));
		result.remove(centeringLine);
		return result;
	}

	/// 表格前后留一点空白（\addvspace 会和前后已有的间距合并，不会叠出一个大空洞），
	/// 并用「量高度 + 比较当前页面剩余空间」决定分页：
	///   - 表格装得进整页时：若当前页剩余空间不够，就整张表换到下一页（不拆开）；
	///   - 表格比整页还高时：只能交给 longtable 拆页，但先判断剩余空间，
	///     剩下太少就从新一页开始，免得只放下一个表头。
	QString wrapTable(const QString &onePage, const QString &multiPage, const QString &skip) {
		const QString height = QStringLiteral("\\dimexpr\\ht\\statementtablebox+\\dp\\statementtablebox\\relax");
		const QString remaining = QStringLiteral("\\dimexpr\\pagegoal-\\pagetotal\\relax");
		return QStringLiteral("\\par\\addvspace{") + skip + QStringLiteral("}%\n") +
		       QStringLiteral("\\sbox{\\statementtablebox}{%\n") + onePage + QStringLiteral("%\n}%\n") +
		       QStringLiteral("\\ifdim") + height + QStringLiteral(">\\textheight\n") +
		       QStringLiteral("\\ifdim") + remaining + QStringLiteral("<.25\\textheight") +
		       QStringLiteral("\\ifdim\\pagetotal>0pt\\newpage\\fi\\fi\n") +
		       // longtable 自带 LTpre / LTpost 上下留白，会和上面的 \addvspace 叠加，
		       // 统一清零，保证与一页表格的上下间距一致。
		       QStringLiteral("\\setlength{\\LTpre}{0pt}\\setlength{\\LTpost}{0pt}%\n") + multiPage +
		       QStringLiteral("\n\\else\n") +
		       QStringLiteral("\\ifdim") + height + QStringLiteral(">") + remaining +
		       QStringLiteral("\\newpage\\fi\n") +
		       QStringLiteral("\\noindent\\begin{minipage}{\\linewidth}\\centering"
		                      "\\setlength{\\parindent}{0pt}%\n\\usebox{\\statementtablebox}%\n"
		                      "\\end{minipage}%\n\\fi\n") +
		       QStringLiteral("\\par\\addvspace{") + skip + QStringLiteral("}%\n");
	}

	/// longtable 表格重排：Tuack 风格（合并单元格）与固定首列两套模式。
	QString convertLongtableBlock(const QString &block, const QString &tableMode, const QString &tableSkip) {
		const QString beginToken = QStringLiteral("\\begin{longtable}");
		const QString endToken = QStringLiteral("\\end{longtable}");
		const int start = block.indexOf(beginToken);

		if (start < 0)
			return block;

		QString blockBody = block.mid(start + beginToken.length());
		QString columnDefinition;

		if (blockBody.startsWith(QChar('['))) {
			const int endBracket = blockBody.indexOf(QChar(']'));

			if (endBracket >= 0)
				blockBody = blockBody.mid(endBracket + 1);
		}

		if (blockBody.startsWith(QChar('{'))) {
			int depth = 0;
			int idx = 0;

			while (idx < blockBody.length()) {
				const QChar ch = blockBody.at(idx);

				if (ch == QChar('{')) {
					depth++;
				} else if (ch == QChar('}')) {
					depth--;

					if (depth == 0) {
						idx++;
						break;
					}
				}

				idx++;
			}

			columnDefinition = blockBody.mid(1, idx - 1 - 1);
			blockBody = blockBody.mid(idx);
		}

		const int end = blockBody.indexOf(endToken);

		if (end < 0)
			return block;

		QString body = blockBody.left(end);
		body.remove(QStringLiteral("\\toprule\\noalign{}"));
		body.remove(QStringLiteral("\\midrule\\noalign{}"));
		body.remove(QStringLiteral("\\bottomrule\\noalign{}"));
		body.remove(QStringLiteral("\\endhead"));
		body.remove(QStringLiteral("\\endlastfoot"));
		body.remove(QStringLiteral("\\endfirsthead"));
		body.remove(QStringLiteral("\\endfoot"));
		body.replace(QStringLiteral("\\tabularnewline"), QStringLiteral("\\\\"));
		body.remove(QStringLiteral("\\arraybackslash"));

		QStringList rows;
		static const QRegularExpression rowSplitter(QStringLiteral(R"(\\\\)"));

		for (const QString &rawRow : body.split(rowSplitter)) {
			QString row = rawRow.trimmed();
			row.remove(QStringLiteral("\\noalign{}"));
			row = row.trimmed();

			if (! row.isEmpty())
				rows.append(row);
		}

		if (rows.isEmpty())
			return {};

		int columnCount = 0;
		QStringList separators;
		QStringList renderedRows = renderMergedRows(rows, columnCount, separators);

		if (tableMode == QStringLiteral("tuacktable")) {
			for (QString &row : renderedRows)
				row = stripMinipage(row);

			const QString header = shiftRowCells(renderedRows.first(), QStringLiteral("-0.75"));
			QStringList bodyText;

			for (int i = 1; i < renderedRows.size(); i++) {
				QString separator = separators.at(i);
				separator.replace(QStringLiteral("\\hline"),
				                  QStringLiteral("\\cline{1-%1}").arg(columnCount));
				bodyText.append(renderedRows.at(i) + QStringLiteral(" \\\\ ") + separator);
			}

			const QString onePage = QStringLiteral("\\begin{statementtable}[%1]{%2}\n%3\n\\end{statementtable}")
			                            .arg(columnCount)
			                            .arg(header, bodyText.join(QChar('\n')));
			const QString multiPage = QStringLiteral("\\begin{tuacktable}[%1]{%2}\n%3\n\\end{tuacktable}")
			                              .arg(columnCount)
			                              .arg(header, bodyText.join(QChar('\n')));
			return wrapTable(onePage, multiPage, tableSkip);
		}

		QString tableSpec;

		if (columnCount == 1) {
			tableSpec = QStringLiteral("|p{3.0cm}|");
		} else {
			const QString otherColumn =
			    QStringLiteral("p{\\dimexpr(\\textwidth-3cm)/%1-2\\tabcolsep\\relax}").arg(columnCount - 1);
			const QString alignments = QString(columnDefinition).remove(QStringLiteral("@{}"));
			QStringList otherColumns;

			for (int columnIndex = 1; columnIndex < columnCount; columnIndex++) {
				const QChar alignment =
				    columnIndex < alignments.length() ? alignments.at(columnIndex) : QChar('l');

				if (alignment == QChar('c'))
					otherColumns.append(QStringLiteral(">{\\centering\\arraybackslash}") + otherColumn);
				else if (alignment == QChar('r'))
					otherColumns.append(QStringLiteral(">{\\raggedleft\\arraybackslash}") + otherColumn);
				else
					otherColumns.append(otherColumn);
			}

			tableSpec = QStringLiteral("|p{3.0cm}|") + otherColumns.join(QChar('|')) + QStringLiteral("|");
		}

		const QString header = renderedRows.first();
		QStringList bodyText;

		for (int i = 1; i < renderedRows.size(); i++)
			bodyText.append(renderedRows.at(i) + QStringLiteral(" \\\\ ") + separators.at(i));

		const QString onePage = QStringLiteral("\\begingroup\\renewcommand{\\arraystretch}{1.0}%\n"
		                                       "\\begin{tabular}{%1}\n\\hline\n%2 \\\\ %3\n%4\n"
		                                       "\\end{tabular}\n\\endgroup")
		                            .arg(tableSpec, header, separators.first(), bodyText.join(QChar('\n')));
		const QString multiPage =
		    QStringLiteral("\\begingroup\\renewcommand{\\arraystretch}{1.0}%\n"
		                   "\\setlength{\\LTleft}{\\fill}\\setlength{\\LTright}{\\fill}%\n"
		                   "\\setlength{\\LTpre}{0pt}\\setlength{\\LTpost}{0pt}%\n"
		                   "\\begin{longtable}{%1}\n\\hline\n%2 \\\\ %3\n\\endfirsthead\n\\hline\n%2 \\\\ %3\n"
		                   "\\endhead\n%4\n\\end{longtable}\n\\endgroup")
		        .arg(tableSpec, header, separators.first(), bodyText.join(QChar('\n')));
		// 表格上下的空白现在由 wrapTable() 统一处理，这里不再额外加，免得叠加。
		return wrapTable(onePage, multiPage, tableSkip);
	}

	QString sanitizeLatex(const QString &source, const QString &tableMode, bool insertPageBreaks,
	                      const QString &tableSkip) {
		QString text = unwrapPassthroughInline(source);
		text.remove(QRegularExpression(QStringLiteral(R"(\\label\{[^}]+\})")));
		text.remove(QStringLiteral("\\begin{Shaded}"));
		text.remove(QStringLiteral("\\end{Shaded}"));
		text.replace(QStringLiteral("\\begin{verbatim}"), QStringLiteral("\\begin{lstlisting}"));
		text.replace(QStringLiteral("\\end{verbatim}"), QStringLiteral("\\end{lstlisting}"));
		text.replace(QRegularExpression(QStringLiteral(R"(\\emph\{([^{}]*)\})")), QStringLiteral("\\file{\\1}"));
		text.remove(QRegularExpression(QStringLiteral(R"(,\s*alt=\{[^{}]*\})")));
		text.remove(QRegularExpression(QStringLiteral(R"(alt=\{[^{}]*\})")));
		text.remove(QStringLiteral("{\\def\\LTcaptype{none} % do not increment counter"));
		text.replace(QRegularExpression(QStringLiteral(R"(\\end\{longtable\}\s*\})")),
		             QStringLiteral("\\end{longtable}"));
		text.replace(QRegularExpression(QStringLiteral(R"(\\\((.*?)\\\))"),
		                                QRegularExpression::DotMatchesEverythingOption),
		             QStringLiteral("$\\1$"));
		text.replace(QRegularExpression(QStringLiteral(R"(\\\[(.*?)\\\])"),
		                                QRegularExpression::DotMatchesEverythingOption),
		             QStringLiteral("$$\\1$$"));

		if (insertPageBreaks)
			text.replace(QStringLiteral("\\section{"), QStringLiteral("\n\\newpage\\section{"));

		{
			const QStringList lines = text.split(QChar('\n'));
			QStringList filtered;

			for (const QString &line : lines)
				if (! line.trimmed().startsWith(QStringLiteral("\\def\\labelenum")))
					filtered.append(line);

			text = filtered.join(QChar('\n'));
		}

		text.remove(QStringLiteral("\\tightlist"));
		text.replace(QRegularExpression(QStringLiteral(R"(\\textbf\{([^{}]+)\})")), QStringLiteral("\\bolddot{\\1}"));

		// longtable → Tuack 表格
		const QString beginToken = QStringLiteral("\\begin{longtable}");
		const QString endToken = QStringLiteral("\\end{longtable}");
		QStringList pieces;
		int start = 0;

		while (true) {
			const int pos = text.indexOf(beginToken, start);

			if (pos < 0) {
				pieces.append(text.mid(start));
				break;
			}

			pieces.append(text.mid(start, pos - start));
			const int end = text.indexOf(endToken, pos);

			if (end < 0) {
				pieces.append(text.mid(pos));
				break;
			}

			const QString block = text.mid(pos, end - pos + endToken.length());
			pieces.append(convertLongtableBlock(block, tableMode, tableSkip));
			start = end + endToken.length();
		}

		text = pieces.join(QString());

		// 图片：独立成段的居中，figure 环境改为无编号居中
		static const QRegularExpression standaloneImage(
		    QStringLiteral(R"((?m)^[ \t]*(\\pandocbounded\{\\includegraphics(?:\[[^\]]*\])?\{[^\n{}]*\}\})[ \t]*\n?)"));
		static const QRegularExpression figureBlock(
		    QStringLiteral(R"((\\pandocbounded\{\\includegraphics(?:\[[^\]]*\])?\{[^\n{}]*\}\}))"));
		static const QRegularExpression captionText(QStringLiteral(R"(\\caption\{([^{}]*)\})"));
		static const QRegularExpression figureEnvironment(
		    QStringLiteral(R"((\n?\\begin\{figure\}.*?\\end\{figure\}))"),
		    QRegularExpression::DotMatchesEverythingOption);

		auto centerWrap = [](QString segment) {
			QString result;
			int position = 0;
			QRegularExpressionMatchIterator it = standaloneImage.globalMatch(segment);

			while (it.hasNext()) {
				const QRegularExpressionMatch matched = it.next();
				result += segment.mid(position, matched.capturedStart() - position);
				result += QStringLiteral("\\begin{center}\n") + matched.captured(1) +
				          QStringLiteral("\n\\end{center}\n");
				position = matched.capturedEnd();
			}

			result += segment.mid(position);
			return result;
		};

		auto unnumberedFigure = [](QString block) {
			const QRegularExpressionMatch image = figureBlock.match(block);

			if (! image.hasMatch())
				return block;

			QStringList lines = {QStringLiteral("\\begin{center}"), image.captured(1), QStringLiteral("\\par")};
			const QRegularExpressionMatch caption = captionText.match(block);

			if (caption.hasMatch() && ! caption.captured(1).trimmed().isEmpty()) {
				lines.append(QStringLiteral("\\vspace{0.6em}"));
				lines.append(caption.captured(1));
			}

			lines.append(QStringLiteral("\\end{center}"));
			return lines.join(QChar('\n')) + QChar('\n');
		};

		QString imageResult;
		const QStringList parts = splitKeepingCaptures(text, figureEnvironment);

		for (const QString &part : parts) {
			if (part.trimmed().startsWith(QStringLiteral("\\begin{figure}")))
				imageResult += unnumberedFigure(part);
			else
				imageResult += centerWrap(part);
		}

		text = imageResult;
		text.replace(tildeToken(), QStringLiteral("$\\sim$"));
		return restoreCjkQuotes(text);
	}

	// ==================================================================================
	// 外部工具
	// ==================================================================================

	struct ToolChain {
		QString pandoc;
		QString xelatex;
		QStringList listingsArgs;
	};

	/// build() 无论从哪个分支返回都把 building 复位。
	struct FlagGuard {
		std::atomic<bool> &flag;
		explicit FlagGuard(std::atomic<bool> &value) : flag(value) { flag.store(true); }
		~FlagGuard() { flag.store(false); }
	};

	int runTool(const QString &program, const QStringList &arguments, const QString &workingDirectory,
	            const QByteArray &input, QByteArray *output, QByteArray *errors) {
		QProcess process;
		Lemon::common::suppressConsoleWindow(process);

		if (! workingDirectory.isEmpty())
			process.setWorkingDirectory(workingDirectory);

		process.setProcessChannelMode(QProcess::SeparateChannels);
		process.start(program, arguments);

		if (! process.waitForStarted(15000))
			return -1;

		if (! input.isEmpty())
			process.write(input);

		process.closeWriteChannel();

		if (! process.waitForFinished(-1)) {
			process.kill();
			process.waitForFinished(3000);
			return -1;
		}

		if (output)
			*output = process.readAllStandardOutput();

		if (errors)
			*errors = process.readAllStandardError();

		return process.exitCode();
	}

	QStringList pandocListingsArgs(const QString &pandoc) {
		const QStringList candidates = {QStringLiteral("--syntax-highlighting=idiomatic"), QStringLiteral("--listings")};

		for (const QString &candidate : candidates) {
			QByteArray out;
			QByteArray err;
			const int code = runTool(pandoc,
			                         {QStringLiteral("-f"), QStringLiteral("markdown"), QStringLiteral("-t"),
			                          QStringLiteral("latex"), candidate},
			                         QString(), QByteArray(), &out, &err);

			if (code == 0)
				return {candidate};
		}

		return {QStringLiteral("--listings")};
	}

	bool pandocToLatex(const ToolChain &tools, const QString &markdown, const QString &workingDirectory,
	                   QString &latex, QString *error) {
		// 题面里列表经常紧贴着上一行正文写（`……：` 下一行就是 `- 项目`），
		// pandoc 默认要求列表前必须空一行，否则会把整个列表并进段落当普通文本。
		// 打开 lists_without_preceding_blankline 才能正确解析成 itemize / enumerate。
		QStringList args = {QStringLiteral("-f"), QStringLiteral("markdown+lists_without_preceding_blankline"),
		                    QStringLiteral("-t"), QStringLiteral("latex"), QStringLiteral("--wrap=none")};
		args += tools.listingsArgs;

		QByteArray out;
		QByteArray err;
		const int code = runTool(tools.pandoc, args, workingDirectory, markdown.toUtf8(), &out, &err);

		if (code != 0) {
			if (error)
				*error = QString::fromLocal8Bit(err).trimmed();

			return false;
		}

		latex = QString::fromUtf8(out);
		// pandoc 在 Windows 下用 CRLF 写 stdout，而这些 \r 一旦跟着进了 .lstlisting，
		// 会被当成额外的换行：代码块每行后面多一个空行、行号翻倍。
		// statement-editor 那边是 read_text() 按文本模式读回来的，天然做了这个归一化。
		latex.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		latex.replace(QChar('\r'), QChar('\n'));
		return true;
	}

	// ==================================================================================
	// 构建流程
	// ==================================================================================

	/// 与 statement-editor 一致：先把 CRLF / CR 统一成 LF，并去掉开头的 BOM。
	QString normalizeSourceText(const QString &rawText) {
		QString text = rawText;
		text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		text.replace(QChar('\r'), QChar('\n'));

		if (! text.isEmpty() && text.at(0) == QChar(0xfeff))
			text.remove(0, 1);

		return text;
	}

	QMap<QString, QString> parseMarkdownMetadata(const QString &rawText, QString &body) {
		QMap<QString, QString> metadata;
		QString text = rawText;
		text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		text.replace(QChar('\r'), QChar('\n'));

		if (! text.isEmpty() && text.at(0) == QChar(0xfeff))
			text.remove(0, 1);

		body = rawText;

		if (! text.startsWith(QStringLiteral("---\n")))
			return metadata;

		const QStringList lines = text.split(QChar('\n'));

		if (lines.size() < 3 || lines.first().trimmed() != QStringLiteral("---"))
			return metadata;

		int endIndex = -1;

		for (int i = 1; i < lines.size(); i++)
			if (lines.at(i).trimmed() == QStringLiteral("---")) {
				endIndex = i;
				break;
			}

		if (endIndex < 0)
			return metadata;

		QStringList header;

		for (int i = 1; i < endIndex; i++)
			header.append(lines.at(i));

		QStringList rest;

		for (int i = endIndex + 1; i < lines.size(); i++)
			rest.append(lines.at(i));

		for (const QString &rawLine : header) {
			const int colon = rawLine.indexOf(QChar(':'));

			if (colon < 0)
				continue;

			QString value = rawLine.mid(colon + 1).trimmed();

			if (value.length() >= 2 &&
			    ((value.startsWith(QChar('"')) && value.endsWith(QChar('"'))) ||
			     (value.startsWith(QChar('\'')) && value.endsWith(QChar('\'')))))
				value = value.mid(1, value.length() - 2);

			metadata.insert(rawLine.left(colon).trimmed().toLower(), value);
		}

		body = rest.join(QChar('\n'));
		return metadata;
	}

	QMap<QString, QString> extractNamedSections(const QString &text, QString &cleaned) {
		static const QRegularExpression pattern(
		    QStringLiteral(R"(<!--\s*SECTION:\s*(?<name>[A-Za-z0-9_\-]+)\s*-->\s*(?<body>.*?)\s*<!--\s*END:\s*\k<name>\s*-->)"),
		    QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
		QMap<QString, QString> sections;
		cleaned = text;
		QStringList pieces;
		int position = 0;
		QRegularExpressionMatchIterator it = pattern.globalMatch(text);

		while (it.hasNext()) {
			const QRegularExpressionMatch matched = it.next();
			const QString name = matched.captured(QStringLiteral("name")).trimmed().toLower();
			sections.insert(name, matched.captured(QStringLiteral("body")).trimmed());
			pieces.append(text.mid(position, matched.capturedStart() - position));
			pieces.append(userSectionMarkers().value(name, QStringLiteral("[%1]").arg(name)) + QChar('\n'));
			position = matched.capturedEnd();
		}

		pieces.append(text.mid(position));
		cleaned = pieces.join(QString()).trimmed();
		return sections;
	}

	QString renderMarkdownInline(const ToolChain &tools, const QString &value) {
		QString text = value;
		text.replace(QChar('\r'), QChar(' '));
		text.replace(QChar('\n'), QChar(' '));
		text = text.trimmed();

		if (text.isEmpty())
			return {};

		QString latex;

		if (! pandocToLatex(tools, metaSentinel() + maskTildes(normalizeInlineMath(text)) + QStringLiteral("\n"),
		                    QString(), latex, nullptr))
			return texEscape(value);

		latex = latex.trimmed();

		if (! latex.startsWith(metaSentinel()))
			return texEscape(value);

		latex = latex.mid(metaSentinel().length());
		latex = unwrapPassthroughInline(latex);
		latex.remove(QStringLiteral("\\tightlist"));
		latex.replace(QRegularExpression(QStringLiteral(R"(\s*\n\s*)")), QStringLiteral(" "));
		latex.replace(QStringLiteral("\\par"), QStringLiteral(" "));
		latex = latex.trimmed();
		latex.replace(tildeToken(), QStringLiteral("$\\sim$"));
		return restoreCjkQuotes(latex);
	}

	QString renderMarkdownSection(const ToolChain &tools, const QString &sectionText, const QString &tableMode,
	                              const QString &tableSkip, QString *error) {
		QString text = sectionText.trimmed();

		if (text.isEmpty())
			return {};

		text = normalizeBlockSeparation(text);
		text = normalizeInlineMath(text);
		text = maskTildes(text);

		QString latex;

		if (! pandocToLatex(tools, text + QStringLiteral("\n"), QString(), latex, error))
			return {};

		return sanitizeLatex(latex, tableMode, true, tableSkip).trimmed();
	}

	QString addSubmissionTitle(const QString &sectionKey, const QString &rendered, const QString &templateName) {
		if (rendered.isEmpty() || ! isNoiTemplate(templateName))
			return rendered;

		const QString title = sectionKey == QStringLiteral("submission_filename")
		                          ? QStringLiteral("提交源程序文件名")
		                          : QStringLiteral("编译选项");
		// 标题前后都用与导览区表格相同的留白长度，这样「表 → 标题 → 表」的间距一致；
		// \addvspace 会和表格自身的留白取较大值合并，不会叠加成一大块空白。
		const QString skip = QStringLiteral("\\statementoverviewtableskip");
		return QStringLiteral("\\noindent{}\\hspace{2em}") + title + QStringLiteral("\\par\\addvspace{") + skip +
		       QStringLiteral("}%\n") + rendered + QStringLiteral("\n\\par\\addvspace{") + skip + QStringLiteral("}%");
	}

	bool buildTexFromMarkdown(const ToolChain &tools, const QString &markdown, QString &generatedLatex,
	                          QMap<QString, QString> &metadata, QMap<QString, QString> &namedSections,
	                          QString *error) {
		QString body;
		metadata = parseMarkdownMetadata(markdown, body);
		namedSections = extractNamedSections(body, body);
		bool hasProblemMarkers = false;
		body = replaceProblemMarkers(body, hasProblemMarkers);
		body = normalizeBlockSeparation(body);
		body = normalizeInlineMath(body);
		body = maskTildes(body);

		if (! pandocToLatex(tools, body, QString(), generatedLatex, error))
			return false;

		generatedLatex = sanitizeLatex(generatedLatex, QStringLiteral("tuacktable"), ! hasProblemMarkers,
		                               QStringLiteral("\\statementtableskip"));
		return true;
	}

	bool injectTemplate(const ToolChain &tools, const QString &templatePath, const QString &templateName,
	                    const QString &generatedText, const QMap<QString, QString> &metadata,
	                    const QMap<QString, QString> &namedSections, QString &outTex, QString *error) {
		QFile file(templatePath);

		if (! file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			if (error)
				*error = QStringLiteral("Cannot read template: %1").arg(templatePath);

			return false;
		}

		const QString templateText = QString::fromUtf8(file.readAll());
		file.close();
		const int begin = templateText.indexOf(QStringLiteral("\\begin{document}"));
		const int end = templateText.indexOf(QStringLiteral("\\end{document}"));

		if (begin < 0 || end < 0) {
			if (error)
				*error = QStringLiteral("Template %1 has no \\begin{document}/\\end{document}.").arg(templatePath);

			return false;
		}

		// 模板内的相对路径（./templates/...）改成工程里模板目录的绝对路径。
		const QString root = QDir::fromNativeSeparators(StatementBuilder::templateRoot());
		QString preamble = templateText.left(begin);
		preamble.replace(QStringLiteral("./templates/"), root + QChar('/'));

		const QString templateDir = root + QChar('/') + templateName + QChar('/');
		QString fontDir = templateDir;

		if (! QFile::exists(templateDir + QStringLiteral("FiraMono-Regular-3.ttf")))
			fontDir = root + QStringLiteral("/fonts/");

		preamble += QString::fromUtf8(highlightPreamble());

		if (! preamble.contains(QStringLiteral("\\setmonofont")) &&
		    ! preamble.contains(QStringLiteral("\\setCJKmonofont")))
			preamble += QStringLiteral("\n\\graphicspath{{%1}}\n"
			                           "\\setmonofont[Path=%2, BoldFont={FiraMono-Medium-2.ttf}]{FiraMono-Regular-3.ttf}\n"
			                           "\\setCJKmonofont[Path=%2]{FiraMono-Regular-3.ttf}\n")
			                .arg(templateDir, fontDir);
		else
			preamble += QStringLiteral("\n\\graphicspath{{%1}}\n").arg(templateDir);

		preamble += QStringLiteral("\\renewcommand{\\bigtitle}{%1}\n").arg(renderMarkdownInline(tools, metadata.value(QStringLiteral("title"))));
		preamble += QStringLiteral("\\renewcommand{\\shorttitle}{%1}\n").arg(renderMarkdownInline(tools, metadata.value(QStringLiteral("subtitle"))));
		preamble += QStringLiteral("\\renewcommand{\\myday}{%1}\n").arg(renderMarkdownInline(tools, metadata.value(QStringLiteral("day"))));
		preamble += QStringLiteral("\\renewcommand{\\mydate}{%1}\n").arg(renderMarkdownInline(tools, metadata.value(QStringLiteral("date"))));

		QString templatePrefix = templateText.mid(begin, end - begin);
		templatePrefix.remove(QStringLiteral("\\begin{document}"));
		templatePrefix.remove(QStringLiteral("\\end{document}"));

		QMap<QString, QString> renderedSections;

		for (auto it = userSectionMarkers().constBegin(); it != userSectionMarkers().constEnd(); ++it) {
			// 导览区那三张表（概览表 / 提交源程序文件名 / 编译选项）用固定首列模式，
			// 上下留白也单独用一个更紧凑的长度；其余（如数据范围）用 tuacktable 模式。
			const bool overview = it.key() == QStringLiteral("overview_table") ||
			                      it.key() == QStringLiteral("submission_filename") ||
			                      it.key() == QStringLiteral("compile_options");
			const QString tableMode = overview ? QStringLiteral("fixed-first-column") : QStringLiteral("tuacktable");
			const QString tableSkip = overview ? QStringLiteral("\\statementoverviewtableskip")
			                                   : QStringLiteral("\\statementtableskip");
			renderedSections.insert(it.key(), renderMarkdownSection(tools, namedSections.value(it.key()), tableMode,
			                                                       tableSkip, error));

			if (error && ! error->isEmpty())
				return false;
		}

		for (const QString &key : noiOnlySections())
			renderedSections.insert(key, addSubmissionTitle(key, renderedSections.value(key), templateName));

		QString generated = generatedText;

		for (auto it = userSectionMarkers().constBegin(); it != userSectionMarkers().constEnd(); ++it) {
			const QString key = it.key();
			const QString marker = it.value();
			const QString rendered = renderedSections.value(key).trimmed();
			const bool inTemplate = templatePrefix.contains(marker);
			const bool inGenerated = generated.contains(marker);

			if (templateName == QStringLiteral("ccpc") && ccpcSkippedSections().contains(key)) {
				templatePrefix.remove(marker);
				generated.remove(marker);
				continue;
			}

			if (noiOnlySections().contains(key) && ! isNoiTemplate(templateName)) {
				generated.remove(marker);
				continue;
			}

			const QString commentNote =
			    QStringLiteral("% %1 omitted by build (no user-provided section)\n").arg(marker);

			if (inTemplate && inGenerated) {
				if (! rendered.isEmpty()) {
					templatePrefix.replace(marker, rendered);
					generated.remove(marker);
				} else {
					templatePrefix.replace(marker, commentNote);
					generated.remove(marker);
				}
			} else if (inTemplate) {
				if (! rendered.isEmpty())
					templatePrefix.replace(marker, rendered);
				else
					templatePrefix.replace(marker, commentNote);
			} else if (inGenerated) {
				if (! rendered.isEmpty())
					generated.replace(marker, rendered);
				else
					generated.replace(marker, commentNote);
			}
		}

		outTex = preamble + QStringLiteral("\n\\begin{document}\n") + templatePrefix + QStringLiteral("\n") +
		         generated + QStringLiteral("\n\\end{document}\n");
		return true;
	}

	void patchImageSupport(QString &outTex, const QStringList &imageDirs) {
		QString graphics;

		for (const QString &dir : imageDirs) {
			const QFileInfo info(dir);

			if (info.isDir())
				graphics += QChar('{') + QDir::fromNativeSeparators(info.absoluteFilePath()) + QChar('}');
		}

		const QString block =
		    QStringLiteral("\n% ---- pandoc image support (injected by LemonLime) ----\n"
		                   "\\makeatletter\n"
		                   "\\newsavebox{\\pandoc@box}\n"
		                   "\\newcommand{\\pandocbounded}[1]{%\n"
		                   "  \\savebox{\\pandoc@box}{#1}%\n"
		                   "  \\ifdim\\wd\\pandoc@box>\\linewidth\n"
		                   "    \\resizebox{\\linewidth}{!}{\\usebox{\\pandoc@box}}%\n"
		                   "  \\else\\usebox{\\pandoc@box}\\fi}\n"
		                   "\\makeatother\n"
		                   "\\graphicspath{%1}\n")
		        .arg(graphics);

		if (outTex.contains(block))
			return;

		outTex.replace(QStringLiteral("\\begin{document}"), block + QStringLiteral("\\begin{document}"), Qt::CaseSensitive);
	}

	QString analyzeLogFile(const QString &logPath) {
		QFile file(logPath);

		if (! file.exists() || ! file.open(QIODevice::ReadOnly))
			return QStringLiteral("No LaTeX log file found: %1").arg(logPath);

		const QStringList lines = QString::fromUtf8(file.readAll()).split(QChar('\n'));
		file.close();
		QStringList blocks;
		QStringList current;

		for (const QString &line : lines) {
			const QString stripped = line.trimmed();

			if (stripped.startsWith(QChar('!'))) {
				if (! current.isEmpty())
					blocks.append(current.join(QChar('\n')).trimmed());

				current = {line};
			} else if (! current.isEmpty()) {
				if (stripped.isEmpty()) {
					blocks.append(current.join(QChar('\n')).trimmed());
					current.clear();
				} else {
					current.append(line);
				}
			}
		}

		if (! current.isEmpty())
			blocks.append(current.join(QChar('\n')).trimmed());

		blocks.removeAll(QString());
		return blocks.join(QStringLiteral("\n\n"));
	}

	void removeSilently(const QString &path) { QFile::remove(path); }

	bool buildStatement(const ToolChain &tools, const QString &sourcePath, const QString &outputBase,
	                    const QString &templateName, const std::function<void(const QString &)> &log,
	                    QString &error) {
		const QFileInfo sourceInfo(sourcePath);

		if (! sourceInfo.exists()) {
			error = QStringLiteral("Statement markdown not found: %1").arg(sourcePath);
			return false;
		}

		QFile file(sourcePath);

		if (! file.open(QIODevice::ReadOnly)) {
			error = QStringLiteral("Cannot read %1").arg(sourcePath);
			return false;
		}

		const QString markdown = normalizeSourceText(QString::fromUtf8(file.readAll()));
		file.close();

		const QString templatePath = QStringLiteral("%1/%2/main.tex").arg(StatementBuilder::templateRoot(), templateName);

		if (! QFile::exists(templatePath)) {
			error = QStringLiteral("Template not found: %1").arg(templatePath);
			return false;
		}

		const QFileInfo outputInfo(outputBase);
		const QString outputDir = outputInfo.absolutePath();
		const QString jobName = outputInfo.fileName();
		const QString pdfPath = outputBase + QStringLiteral(".pdf");
		const QString logPath = outputBase + QStringLiteral(".log");
		const QString texPath = outputBase + QStringLiteral(".tex");

		QTemporaryDir temporary;

		if (! temporary.isValid()) {
			error = QStringLiteral("Cannot create a temporary directory for the statement build.");
			return false;
		}

		// 1. markdown → LaTeX（区段、试题、元数据）
		QMap<QString, QString> metadata;
		QMap<QString, QString> namedSections;
		QString generatedLatex;

		if (! buildTexFromMarkdown(tools, markdown, generatedLatex, metadata, namedSections, &error)) {
			if (error.isEmpty())
				error = QStringLiteral("pandoc failed to convert the statement markdown.");

			return false;
		}

		log(QStringLiteral("pandoc: markdown converted to LaTeX"));

		// 2. 拼装完整文档（模板 + 导言区 + 区段）
		QString outTex;

		if (! injectTemplate(tools, templatePath, templateName, generatedLatex, metadata, namedSections, outTex,
		                     &error))
			return false;

		patchImageSupport(outTex, {sourceInfo.absolutePath(), outputDir,
		                           QStringLiteral("%1/%2").arg(StatementBuilder::templateRoot(), templateName)});

		const QString texFile = temporary.path() + QChar('/') + jobName + QStringLiteral(".tex");

		{
			QFile tex(texFile);

			if (! tex.open(QIODevice::WriteOnly | QIODevice::Text)) {
				error = QStringLiteral("Cannot write %1").arg(texFile);
				return false;
			}

			QTextStream stream(&tex);
			stream << outTex;
		}

		removeSilently(pdfPath);

		// 3. xelatex 跑两遍
		const QStringList args = {QStringLiteral("-interaction=nonstopmode"), QStringLiteral("-halt-on-error"),
		                          QStringLiteral("-output-directory"), outputDir, texFile};

		for (int pass = 0; pass < 2; pass++) {
			QByteArray out;
			QByteArray err;
			const int code = runTool(tools.xelatex, args, temporary.path(), QByteArray(), &out, &err);
			log(QStringLiteral("xelatex pass %1 finished with code %2").arg(pass + 1).arg(code));

			if (code != 0) {
				const QString report = analyzeLogFile(logPath);

				if (report.isEmpty())
					error = QStringLiteral("xelatex failed (exit code %1).").arg(code);
				else
					error = report;

				// 失败时保留 .tex 与 .log，便于排查
				{
					QFile kept(texPath);

					if (kept.open(QIODevice::WriteOnly | QIODevice::Text)) {
						QTextStream stream(&kept);
						stream << outTex;
					}
				}

				return false;
			}
		}

		if (! QFile::exists(pdfPath)) {
			error = QStringLiteral("xelatex finished but %1 was not produced.").arg(pdfPath);
			return false;
		}

		// 4. 清理中间产物，只留 statement.md 与 statement.pdf
		for (const QString &suffix : {QStringLiteral(".aux"), QStringLiteral(".out"), QStringLiteral(".toc"),
		                              QStringLiteral(".lof"), QStringLiteral(".lot"), QStringLiteral(".bcf"),
		                              QStringLiteral(".run.xml"), QStringLiteral(".synctex.gz"),
		                              QStringLiteral(".fls"), QStringLiteral(".fdb_latexmk"), QStringLiteral(".blg"),
		                              QStringLiteral(".bbl"), QStringLiteral(".log"), QStringLiteral(".tex")})
			removeSilently(outputBase + suffix);

		return true;
	}
} // namespace

QStringList StatementBuilder::templateNames() {
	return {QStringLiteral("ccpc"), QStringLiteral("noi"), QStringLiteral("noi new")};
}

QString StatementBuilder::templateRoot() {
	static const QString cached = [] {
		// 发布时模板放在 lemon.exe 旁边，开发时在工程目录里（build 的上一级）。
		const QStringList candidates = {
		    QCoreApplication::applicationDirPath() + QStringLiteral("/statement-templates"),
		    QCoreApplication::applicationDirPath() + QStringLiteral("/assets/statement-templates"),
		    QCoreApplication::applicationDirPath() + QStringLiteral("/../statement-templates"),
		    QCoreApplication::applicationDirPath() + QStringLiteral("/../assets/statement-templates"),
		};

		for (const QString &candidate : candidates) {
			const QFileInfo info(candidate);

			if (info.isDir() && QFile::exists(info.absoluteFilePath() + QStringLiteral("/noi new/main.tex")))
				return QDir::fromNativeSeparators(info.absoluteFilePath());
		}

		return QString();
	}();
	return cached;
}

QString StatementBuilder::toolsReport() {
	const QString pandoc = QStandardPaths::findExecutable(QStringLiteral("pandoc"));
	const QString xelatex = QStandardPaths::findExecutable(QStringLiteral("xelatex"));
	QStringList missing;

	if (pandoc.isEmpty())
		missing.append(QStringLiteral("pandoc"));

	if (xelatex.isEmpty())
		missing.append(QStringLiteral("xelatex"));

	if (templateRoot().isEmpty())
		missing.append(QStringLiteral("statement-templates"));

	return missing.join(QStringLiteral(", "));
}

StatementBuilder::StatementBuilder(QObject *parent) : QObject(parent), templateName(QStringLiteral("noi new")) {}

void StatementBuilder::setTemplate(const QString &name) { templateName = name; }

void StatementBuilder::setSourceFile(const QString &path) { sourceFile = path; }

void StatementBuilder::setOutputBase(const QString &pathWithoutSuffix) { outputBase = pathWithoutSuffix; }

void StatementBuilder::log(const QString &message) { emit logMessage(message); }

void StatementBuilder::fail(const QString &message) { errorText = message; }

bool StatementBuilder::build() {
	errorText.clear();

	if (building.load()) {
		fail(tr("A statement build is already running."));
		return false;
	}

	FlagGuard guard(building);
	const QString root = templateRoot();

	if (root.isEmpty()) {
		fail(tr("Cannot find the statement templates (statement-templates)."));
		return false;
	}

	const QString pandoc = QStandardPaths::findExecutable(QStringLiteral("pandoc"));

	if (pandoc.isEmpty()) {
		fail(tr("pandoc not found in PATH."));
		return false;
	}

	const QString xelatex = QStandardPaths::findExecutable(QStringLiteral("xelatex"));

	if (xelatex.isEmpty()) {
		fail(tr("xelatex not found in PATH."));
		return false;
	}

	ToolChain tools;
	tools.pandoc = pandoc;
	tools.xelatex = xelatex;
	tools.listingsArgs = pandocListingsArgs(pandoc);

	log(tr("Building with template \"%1\" ...").arg(templateName));

	QString message;

	if (! buildStatement(tools, sourceFile, outputBase, templateName,
	                     [this](const QString &line) { log(line); }, message)) {
		fail(message.isEmpty() ? tr("Building the statement PDF failed.") : message);
		return false;
	}

	log(tr("Statement PDF written to %1").arg(outputBase + QStringLiteral(".pdf")));
	return true;
}
