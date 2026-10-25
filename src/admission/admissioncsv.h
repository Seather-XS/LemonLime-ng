/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct AdmissionTable {
	QStringList header;
	QList<QStringList> rows;

	static QStringList builtinColumns();

	void normalize();
	int columnIndex(const QString &name) const;
	QString cell(int row, int column) const;

	QStringList rowAt(int index) const;
};

namespace AdmissionCsv {
	bool parse(const QString &text, AdmissionTable &table, QString *error);
	QString write(const AdmissionTable &table);
	bool readFile(const QString &path, AdmissionTable &table, QString *error);
	bool writeFile(const QString &path, const AdmissionTable &table, QString *error);
	bool writeTemplate(const QString &path, const QStringList &header, QString *error);
	QString escape(const QString &value);
	bool looksLikeGbk(const QByteArray &raw);
} 
