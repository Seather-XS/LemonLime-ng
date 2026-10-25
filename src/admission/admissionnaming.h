/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

struct AdmissionNamingContext {
	QString section;
	QString name;
	QString id;
	QString seat;
	QString room;
	int row{0};
	int col{0};
	int regionSeq{0};
	int globalSeq{0};
	QMap<QString, QString> columns;
};

namespace AdmissionNaming {
	QStringList reservedKeywords();
	int groupCount(const QString &pattern, const QString &keyword);
	bool validate(const QString &pattern, const QStringList &columns, QString *error);
	QString render(const QString &pattern, const AdmissionNamingContext &context,
	               const QStringList &numberSources, const QStringList &charSources, QString *error);
	QString describe(const QString &pattern);
	QString sanitize(const QString &name, const QString &replacement, int maxLength);
	QString excelLetters(int value);
}
