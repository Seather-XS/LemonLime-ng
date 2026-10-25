/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct ViolationRule {
	QString kind; 
	QString text;

	bool isCommand() const { return kind == QStringLiteral("command"); }
};

struct ViolationLanguage {
	QString name;
	QStringList compilers;
	QStringList extensions; 
	QVector<ViolationRule> rules;
};

class Violation {
  public:
	struct Match {
		ViolationRule rule;
		int line = 0;
		QString snippet;
	};

	static QString kindCommand() { return QStringLiteral("command"); }
	static QString kindString() { return QStringLiteral("string"); }

	static QVector<ViolationRule> defaultRules();

	static const QVector<ViolationLanguage> &languages();

	static const ViolationLanguage *languageFor(const QString &compilerName,
	                                            const QStringList &sourceExtensions);

	static QString languageNameFor(const QString &compilerName, const QStringList &sourceExtensions);

	static QVector<ViolationRule> defaultRulesFor(const QString &compilerName,
	                                              const QStringList &sourceExtensions);

	static QVector<ViolationRule> defaultRulesFor(const QStringList &sourceExtensions);

	static QVector<ViolationRule> normalizeRules(const QVector<ViolationRule> &rules);

	static QString maskCode(const QString &text);

	static QVector<Match> checkSource(const QString &text, const QVector<ViolationRule> &rules);

	static bool checkFiles(const QStringList &paths, const QVector<ViolationRule> &rules, QString *outPath,
	                       QVector<Match> *outMatches);

	static QString describe(const QString &path, const QVector<Match> &matches);

	static QString shortText(const QString &path, const QVector<Match> &matches);
};
