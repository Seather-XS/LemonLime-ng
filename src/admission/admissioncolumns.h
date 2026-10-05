/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "admissioncsv.h"
#include "admissionnaming.h"
//
#include <QList>
#include <QString>
#include <QStringList>

/** 一列的生成规则（columns.json 里的一项）。 */
struct AdmissionColumnRule {
	QString name;
	QString type{QStringLiteral("template")}; ///< template / random
	QString templateText;                     ///< type=template
	QStringList numberSources;                ///< 按组给来源，见 AdmissionNaming::render
	QStringList charSources;
	int length{8};                            ///< type=random
	bool upper{true};
	bool lower{true};
	bool digit{true};
	bool symbol{false};
	bool excludeAmbiguous{true};
	bool recalculable{false};
	QString seedSource{QStringLiteral("row")}; ///< row / id / name
};

/** columns.json：每赛区一份，描述哪些列按模板生成、哪些列随机。 */
namespace AdmissionColumns {
	bool load(const QString &region, QList<AdmissionColumnRule> &rules, QString *error);
	bool save(const QString &region, const QList<AdmissionColumnRule> &rules, QString *error);

	/// 按列序求值：模板列从左到右处理，引用必须指向已经求值的列（否则报错）；
	/// 随机列只填空值，可重算的按 seedSource 生成（同一行永远一样）。
	bool evaluate(const QList<AdmissionColumnRule> &rules, AdmissionTable &table, const QString &section,
	              bool onlyEmpty, QString *error);

	/// 给界面做预览：模板列算一遍，随机列真生成一个值。
	QString preview(const AdmissionColumnRule &rule, const AdmissionNamingContext &context, QString *error);
} // namespace AdmissionColumns
