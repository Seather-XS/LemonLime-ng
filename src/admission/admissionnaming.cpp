/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "admissionnaming.h"
//
#include <QObject>
#include <QRegularExpression>

namespace AdmissionNaming {
	namespace {
		struct Token {
			QString text;
			bool placeholder{false};
		};

		bool tokenize(const QString &pattern, QList<Token> &tokens, QString *error) {
			for (int position = 0; position < pattern.length();) {
				const QChar ch = pattern.at(position);

				if (ch != QChar('<')) {
					Token token;
					token.text = ch;
					tokens << token;
					++position;
					continue;
				}

				const int close = pattern.indexOf(QChar('>'), position);

				if (close < 0) {
					if (error)
						*error = QObject::tr("The placeholder starting at %1 is not closed.").arg(position + 1);
					return false;
				}

				Token token;
				token.placeholder = true;
				token.text = pattern.mid(position + 1, close - position - 1).trimmed();
				tokens << token;
				position = close + 1;
			}

			return true;
		}

		int sourceValue(const QString &source, const AdmissionNamingContext &context, bool *ok) {
			*ok = true;
			const QString key = source.trimmed();

			if (key.isEmpty() || key == QStringLiteral("regionSeq"))
				return context.regionSeq;

			if (key == QStringLiteral("globalSeq"))
				return context.globalSeq;

			if (key == QStringLiteral("row"))
				return context.row;

			if (key == QStringLiteral("col"))
				return context.col;

			if (key == QStringLiteral("seat"))
				return context.seat.toInt(ok);

			if (key == QStringLiteral("room"))
				return context.room.toInt(ok);

			if (key.startsWith(QStringLiteral("column:")))
				return context.columns.value(key.mid(7)).toInt(ok);

			if (key == QStringLiteral("id")) {
				// 准考证号里的数字尾部：HN-S01192 → 1192
				static const QRegularExpression trailing(QStringLiteral(R"((\d+)\s*$)"));
				const QRegularExpressionMatch match = trailing.match(context.id);

				if (match.hasMatch())
					return match.captured(1).toInt(ok);
			}

			if (key == QStringLiteral("name")) {
				static const QRegularExpression trailing(QStringLiteral(R"((\d+)\s*$)"));
				const QRegularExpressionMatch match = trailing.match(context.name);

				if (match.hasMatch())
					return match.captured(1).toInt(ok);
			}

			*ok = false;
			return context.regionSeq;
		}
	} // namespace

	QStringList reservedKeywords() {
		return {QStringLiteral("section"), QStringLiteral("number"), QStringLiteral("char"),
		        QStringLiteral("row"),     QStringLiteral("col"),    QStringLiteral("name"),
		        QStringLiteral("id"),      QStringLiteral("seat"),   QStringLiteral("room")};
	}

	int groupCount(const QString &pattern, const QString &keyword) {
		QList<Token> tokens;

		if (! tokenize(pattern, tokens, nullptr))
			return 0;

		int groups = 0;
		bool inside = false;

		for (const Token &token : tokens) {
			const bool hit = token.placeholder && token.text == keyword;

			if (hit && ! inside)
				++groups;

			inside = hit;
		}

		return groups;
	}

	bool validate(const QString &pattern, const QStringList &columns, QString *error) {
		QList<Token> tokens;

		if (! tokenize(pattern, tokens, error))
			return false;

		if (pattern.trimmed().isEmpty()) {
			if (error)
				*error = QObject::tr("The template is empty.");
			return false;
		}

		for (const QString &keyword : {QStringLiteral("number"), QStringLiteral("char")}) {
			const int groups = groupCount(pattern, keyword);

			if (groups > 2) {
				if (error)
					*error = QObject::tr("At most 2 groups of <%1> are supported, found %2.")
					             .arg(keyword)
					             .arg(groups);
				return false;
			}
		}

		for (const Token &token : tokens) {
			if (! token.placeholder)
				continue;

			if (token.text == QStringLiteral("number") || token.text == QStringLiteral("char"))
				continue;

			if (reservedKeywords().contains(token.text))
				continue;

			if (token.text.startsWith(QStringLiteral("column:"))) {
				if (! columns.contains(token.text.mid(7))) {
					if (error)
						*error = QObject::tr("No column named %1.").arg(token.text.mid(7));
					return false;
				}

				continue;
			}

			if (! columns.contains(token.text)) {
				if (error)
					*error = QObject::tr("Unknown placeholder <%1>.").arg(token.text);
				return false;
			}
		}

		return true;
	}

	QString render(const QString &pattern, const AdmissionNamingContext &context,
	               const QStringList &numberSources, const QStringList &charSources, QString *error) {
		QList<Token> tokens;

		if (! tokenize(pattern, tokens, error))
			return {};

		QString out;
		int numberGroup = -1;
		int charGroup = -1;
		bool insideNumber = false;
		bool insideChar = false;

		for (int index = 0; index < tokens.size();) {
			const Token &token = tokens.at(index);

			if (! token.placeholder) {
				out += token.text;
				insideNumber = false;
				insideChar = false;
				++index;
				continue;
			}

			if (token.text == QStringLiteral("number") || token.text == QStringLiteral("char")) {
				const bool isNumber = token.text == QStringLiteral("number");
				bool &inside = isNumber ? insideNumber : insideChar;
				int &group = isNumber ? numberGroup : charGroup;

				if (! inside) {
					inside = true;
					++group;
				}

				// 先把这一组的宽度数出来（连续同类算一组）
				int width = 0;
				int scan = index;

				while (scan < tokens.size() && tokens.at(scan).placeholder &&
				       tokens.at(scan).text == token.text) {
					++width;
					++scan;
				}

				const QStringList &sources = isNumber ? numberSources : charSources;
				const QString source = sources.value(group, QStringLiteral("regionSeq"));
				bool ok = false;
				const int value = sourceValue(source, context, &ok);

				if (isNumber) {
					QString text = QString::number(qMax(0, value));

					if (text.size() < width)
						text = QString(width - text.size(), QChar('0')) + text;

					out += text;
				} else {
					out += excelLetters(qMax(1, value));
				}

				index = scan;
				insideNumber = isNumber;
				insideChar = ! isNumber;
				continue;
			}

			insideNumber = false;
			insideChar = false;
			const QString key = token.text;

			if (key == QStringLiteral("section"))
				out += context.section;
			else if (key == QStringLiteral("name"))
				out += context.name;
			else if (key == QStringLiteral("id"))
				out += context.id;
			else if (key == QStringLiteral("seat"))
				out += context.seat;
			else if (key == QStringLiteral("room"))
				out += context.room;
			else if (key == QStringLiteral("row"))
				out += QString::number(context.row);
			else if (key == QStringLiteral("col"))
				out += QString::number(context.col);
			else if (key.startsWith(QStringLiteral("column:")))
				out += context.columns.value(key.mid(7));
			else
				out += context.columns.value(key);

			++index;
		}

		return out;
	}

	QString describe(const QString &pattern) {
		QStringList parts;
		const int numbers = groupCount(pattern, QStringLiteral("number"));
		const int chars = groupCount(pattern, QStringLiteral("char"));

		if (numbers > 0)
			parts << QObject::tr("%1 number group(s)").arg(numbers);

		if (chars > 0)
			parts << QObject::tr("%1 letter group(s)").arg(chars);

		return parts.isEmpty() ? QObject::tr("no sequence") : parts.join(QStringLiteral(", "));
	}

	QString sanitize(const QString &name, const QString &replacement, int maxLength) {
		static const QRegularExpression illegal(QStringLiteral(R"([\\/:*?"<>|\x00-\x1F])"));
		const QString filler = replacement.isEmpty() ? QStringLiteral("_") : replacement;
		QString out = name;
		out.replace(illegal, filler);

		while (out.endsWith(QChar('.')) || out.endsWith(QChar(' ')))
			out.chop(1);

		if (maxLength > 0 && out.size() > maxLength)
			out = out.left(maxLength);

		return out.trimmed();
	}

	QString excelLetters(int value) {
		if (value <= 0)
			return QStringLiteral("A");

		QString out;
		int current = value;

		while (current > 0) {
			const int remainder = (current - 1) % 26;
			out.prepend(QChar('A' + remainder));
			current = (current - 1) / 26;
		}

		return out.right(2);
	}
} // namespace AdmissionNaming
