/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "admissionnotes.h"
//
#include <QRegularExpression>

namespace AdmissionNotes {
	QString escape(const QString &text) {
		QString out;

		for (const QChar ch : text) {
			switch (ch.unicode()) {
				case '\\': out += QStringLiteral("\\textbackslash{}"); break;
				case '{': out += QStringLiteral("\\{"); break;
				case '}': out += QStringLiteral("\\}"); break;
				case '$': out += QStringLiteral("\\$"); break;
				case '&': out += QStringLiteral("\\&"); break;
				case '#': out += QStringLiteral("\\#"); break;
				case '%': out += QStringLiteral("\\%"); break;
				case '_': out += QStringLiteral("\\_"); break;
				case '~': out += QStringLiteral("\\textasciitilde{}"); break;
				case '^': out += QStringLiteral("\\textasciicircum{}"); break;
				case '\n': out += QChar(' '); break;
				default: out += ch; break;
			}
		}

		return out;
	}

	namespace {
		/// `\href{...}` 里那一段也要转义：URL 里的 & % # _ 直接写会把 hyperref 弄崩。
		QString escapeUrl(const QString &url) {
			QString out;

			for (const QChar ch : url) {
				switch (ch.unicode()) {
					case '\\': out += QStringLiteral("\\textbackslash{}"); break;
					case '{': out += QStringLiteral("\\{"); break;
					case '}': out += QStringLiteral("\\}"); break;
					case '$': out += QStringLiteral("\\$"); break;
					case '&': out += QStringLiteral("\\&"); break;
					case '#': out += QStringLiteral("\\#"); break;
					case '%': out += QStringLiteral("\\%"); break;
					case '_': out += QStringLiteral("\\_"); break;
					case '~': out += QStringLiteral("\\textasciitilde{}"); break;
					default: out += ch; break;
				}
			}

			return out;
		}

		/// `[文字](链接)`：链接里不能有空格或圆括号，否则就按普通文本处理。
		const QRegularExpression &linkPattern() {
			static const QRegularExpression pattern(QStringLiteral(R"(\[([^\[\]]*)\]\(([^()\s]*)\))"));
			return pattern;
		}

		/// 一行：先转义普通文本，再把超链接换成 \href。
		QString renderLine(const QString &line) {
			QString out;
			int last = 0;
			auto iterator = linkPattern().globalMatch(line);

			while (iterator.hasNext()) {
				const QRegularExpressionMatch match = iterator.next();
				out += escape(line.mid(last, match.capturedStart() - last));
				out += QStringLiteral("\\href{%1}{%2}")
				           .arg(escapeUrl(match.captured(2)), escape(match.captured(1)));
				last = match.capturedEnd();
			}

			out += escape(line.mid(last));
			return out.trimmed();
		}
	} // namespace

	QString toLatex(const QString &text) {
		QString normalized = text;
		normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		normalized.replace(QChar('\r'), QChar('\n'));

		QStringList paragraphs;
		QStringList lines;

		for (const QString &raw : normalized.split(QChar('\n'))) {
			const QString line = raw.trimmed();

			if (line.isEmpty()) {
				// 空行 = 分段
				if (! lines.isEmpty()) {
					paragraphs << lines.join(QStringLiteral("\\newline\n"));
					lines.clear();
				}

				continue;
			}

			lines << renderLine(line);
		}

		if (! lines.isEmpty())
			paragraphs << lines.join(QStringLiteral("\\newline\n"));

		return paragraphs.join(QStringLiteral("\\par\n"));
	}
} // namespace AdmissionNotes
