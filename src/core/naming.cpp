/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "naming.h"

#include <QDir>
#include <QRegularExpression>
#include <QStringList>

namespace {
	const QString PLACEHOLDER_SECTION = QStringLiteral("<section>");
	const QString PLACEHOLDER_NUMBER = QStringLiteral("<number>");
	const QString PLACEHOLDER_CHAR = QStringLiteral("<char>");
} // namespace

auto Naming::defaultPattern() -> QString {
	return QStringLiteral("<section>-S<number><number><number><number><number>");
}

auto Naming::parameters() -> QStringList {
	return {PLACEHOLDER_SECTION, PLACEHOLDER_NUMBER, PLACEHOLDER_CHAR};
}

auto Naming::toRegexBody(const QString &pattern, const QString &section) -> QString {
	QString body;
	const int size = pattern.size();
	int index = 0;

	while (index < size) {
		if (pattern.mid(index).startsWith(PLACEHOLDER_SECTION)) {
			body += QRegularExpression::escape(section);
			index += PLACEHOLDER_SECTION.size();
		} else if (pattern.mid(index).startsWith(PLACEHOLDER_NUMBER)) {
			body += QStringLiteral("[0-9]");
			index += PLACEHOLDER_NUMBER.size();
		} else if (pattern.mid(index).startsWith(PLACEHOLDER_CHAR)) {
			body += QStringLiteral("[A-Za-z]");
			index += PLACEHOLDER_CHAR.size();
		} else { // 普通字符：按字面量
			body += QRegularExpression::escape(QString(pattern.at(index)));
			index += 1;
		}
	}

	return body;
}

bool Naming::matches(const QString &name, const QString &section, const QString &pattern) {
	if (pattern.isEmpty())
		return true;

	const QRegularExpression re(QStringLiteral("\\A") + toRegexBody(pattern, section) + QStringLiteral("\\z"));
	return re.match(name).hasMatch();
}

auto Naming::sample(const QString &pattern, const QString &section) -> QString {
	QString text = pattern;
	text.replace(PLACEHOLDER_SECTION, section);
	text.replace(PLACEHOLDER_NUMBER, QStringLiteral("x"));
	text.replace(PLACEHOLDER_CHAR, QStringLiteral("c"));
	return text;
}

auto Naming::describe(const QString &name, const QString &section, const QString &pattern) -> QString {
	const QString where = section.isEmpty()
	                          ? QStringLiteral("本比赛日（未启用赛区）的选手")
	                          : QStringLiteral("赛区「%1」的选手").arg(section);
	return QStringLiteral("%1文件夹名「%2」不符合命名限制「%3」（应形如 %4）")
	    .arg(where, name, pattern, sample(pattern, section));
}

auto Naming::folderName(const QString &path) -> QString {
	return QDir::cleanPath(path).section(QLatin1Char('/'), -1);
}
