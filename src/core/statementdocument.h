/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class StatementProblem {
  public:
	static constexpr int ProinfoCount = 4;

	QString title;
	QString english;
	QStringList body;  
	QStringList proinfo;

	QString heading() const;

	static bool parseProinfoLine(const QString &line, QStringList &values);
	static QString proinfoComment(const QStringList &values);
	static QStringList normalizedProinfo(QStringList values);
};

class StatementDocument {
  public:
	static const QStringList &knownSections();
	static const QStringList &editableMetaKeys();
	static QString problemMarker();

	static bool headingOf(const QString &line, QString &title, QString &english);

	static StatementDocument parse(const QString &text);

	QString toMarkdown() const;

	bool hasFrontMatter() const { return hasFront; }
	QString metadata(const QString &key) const;
	void setMetadata(const QString &key, const QString &value);

	QStringList sectionNames() const;
	bool hasSection(const QString &name) const;
	QString sectionBody(const QString &name) const;
	void setSectionBody(const QString &name, const QString &body);

	QString headText() const;

	QList<StatementProblem> problems() const;
	void setProblems(const QList<StatementProblem> &problems);

  private:
	struct Item {
		bool isSection{false};
		QString text;
		QString name;
		QString body;  
	};

	static QStringList splitLines(const QString &text);
	static bool isProblemMarker(const QString &line);
	static bool matchProblemHeading(const QString &line, QString &title, QString &english);
	static QString problemTitleText(const QString &line);

	static void splitProblemArea(const QString &text, QStringList &head, QList<StatementProblem> &problems);
	static QString serializeProblemArea(const QStringList &head, const QList<StatementProblem> &problems);

	int problemsItem() const;
	QString problemsText() const;

	bool hasFront{false};
	QList<QPair<QString, QString>> metaEntries;
	QMap<QString, QString> metaValues;           
	QList<Item> items;
};
