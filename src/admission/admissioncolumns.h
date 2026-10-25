/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "admissioncsv.h"
#include "admissionnaming.h"
#include <QList>
#include <QString>
#include <QStringList>

struct AdmissionColumnRule {
	QString name;
	QString type{QStringLiteral("template")};
	QString templateText;
	QStringList numberSources;
	QStringList charSources;
	int length{8};
	bool upper{true};
	bool lower{true};
	bool digit{true};
	bool symbol{false};
	bool excludeAmbiguous{true};
	bool recalculable{false};
	QString seedSource{QStringLiteral("row")};
};

namespace AdmissionColumns {
	bool load(const QString &region, QList<AdmissionColumnRule> &rules, QString *error);
	bool save(const QString &region, const QList<AdmissionColumnRule> &rules, QString *error);

	bool evaluate(const QList<AdmissionColumnRule> &rules, AdmissionTable &table, const QString &section,
	              bool onlyEmpty, QString *error);

	QString preview(const AdmissionColumnRule &rule, const AdmissionNamingContext &context, QString *error);
}
