/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "admissioncolumns.h"
//
#include "admissionproject.h"
//
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRandomGenerator>

namespace AdmissionColumns {
	namespace {
		QStringList jsonToStrings(const QJsonArray &array) {
			QStringList values;

			for (const auto &value : array)
				values << value.toString();

			return values;
		}

		QJsonArray stringsToJson(const QStringList &values) {
			QJsonArray array;

			for (const QString &value : values)
				array.append(value);

			return array;
		}

		/// 可重算的随机列：按种子算出稳定的串（同一行永远一样）。
		QString seededRandom(const QString &seed, const AdmissionColumnRule &rule) {
			QString alphabet;

			if (rule.upper)
				alphabet += QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");

			if (rule.lower)
				alphabet += QStringLiteral("abcdefghijklmnopqrstuvwxyz");

			if (rule.digit)
				alphabet += QStringLiteral("0123456789");

			if (rule.symbol)
				alphabet += QStringLiteral("!@#$%^&*()-_=+[]{}");

			if (rule.excludeAmbiguous)
				alphabet.remove(QRegularExpression(QStringLiteral("[Il1O0o|]")));

			if (alphabet.isEmpty())
				alphabet = QStringLiteral("0123456789");

			QByteArray digest = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256);
			QString out;

			for (int i = 0; i < rule.length; ++i)
				out += alphabet.at(quint8(digest.at(i % digest.size())) % alphabet.size());

			return out;
		}

		QString freshRandom(const AdmissionColumnRule &rule) {
			QString alphabet;

			if (rule.upper)
				alphabet += QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");

			if (rule.lower)
				alphabet += QStringLiteral("abcdefghijklmnopqrstuvwxyz");

			if (rule.digit)
				alphabet += QStringLiteral("0123456789");

			if (rule.symbol)
				alphabet += QStringLiteral("!@#$%^&*()-_=+[]{}");

			if (rule.excludeAmbiguous)
				alphabet.remove(QRegularExpression(QStringLiteral("[Il1O0o|]")));

			if (alphabet.isEmpty())
				alphabet = QStringLiteral("0123456789");

			QString out;

			for (int i = 0; i < rule.length; ++i)
				out += alphabet.at(int(QRandomGenerator::global()->bounded(alphabet.size())));

			return out;
		}
	} // namespace

	bool load(const QString &region, QList<AdmissionColumnRule> &rules, QString *error) {
		rules.clear();
		QFile file(AdmissionProject::columnsPath(region));

		if (! file.exists())
			return true;

		if (! file.open(QIODevice::ReadOnly)) {
			if (error)
				*error = QObject::tr("Cannot read %1").arg(AdmissionProject::columnsPath(region));
			return false;
		}

		const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
		file.close();

		for (const auto &value : object.value(QStringLiteral("columns")).toArray()) {
			const QJsonObject item = value.toObject();
			AdmissionColumnRule rule;
			rule.name = item.value(QStringLiteral("name")).toString();
			rule.type = item.value(QStringLiteral("type")).toString(rule.type);
			rule.templateText = item.value(QStringLiteral("template")).toString();
			rule.numberSources = jsonToStrings(item.value(QStringLiteral("numberGroups")).toArray());
			rule.charSources = jsonToStrings(item.value(QStringLiteral("charGroups")).toArray());
			rule.length = item.value(QStringLiteral("length")).toInt(rule.length);
			const QJsonObject charset = item.value(QStringLiteral("charset")).toObject();
			rule.upper = charset.value(QStringLiteral("upper")).toBool(rule.upper);
			rule.lower = charset.value(QStringLiteral("lower")).toBool(rule.lower);
			rule.digit = charset.value(QStringLiteral("digit")).toBool(rule.digit);
			rule.symbol = charset.value(QStringLiteral("symbol")).toBool(rule.symbol);
			rule.excludeAmbiguous = item.value(QStringLiteral("excludeAmbiguous")).toBool(rule.excludeAmbiguous);
			rule.recalculable = item.value(QStringLiteral("recalculable")).toBool(rule.recalculable);
			rule.seedSource = item.value(QStringLiteral("seedSource")).toString(rule.seedSource);

			if (! rule.name.trimmed().isEmpty())
				rules << rule;
		}

		return true;
	}

	bool save(const QString &region, const QList<AdmissionColumnRule> &rules, QString *error) {
		QJsonArray array;

		for (const AdmissionColumnRule &rule : rules) {
			QJsonObject item;
			item.insert(QStringLiteral("name"), rule.name);
			item.insert(QStringLiteral("type"), rule.type);

			if (rule.type == QStringLiteral("template")) {
				item.insert(QStringLiteral("template"), rule.templateText);
				item.insert(QStringLiteral("numberGroups"), stringsToJson(rule.numberSources));
				item.insert(QStringLiteral("charGroups"), stringsToJson(rule.charSources));
			} else {
				QJsonObject charset;
				charset.insert(QStringLiteral("upper"), rule.upper);
				charset.insert(QStringLiteral("lower"), rule.lower);
				charset.insert(QStringLiteral("digit"), rule.digit);
				charset.insert(QStringLiteral("symbol"), rule.symbol);
				item.insert(QStringLiteral("length"), rule.length);
				item.insert(QStringLiteral("charset"), charset);
				item.insert(QStringLiteral("excludeAmbiguous"), rule.excludeAmbiguous);
				item.insert(QStringLiteral("recalculable"), rule.recalculable);
				item.insert(QStringLiteral("seedSource"), rule.seedSource);
			}

			array.append(item);
		}

		QJsonObject object;
		object.insert(QStringLiteral("columns"), array);

		QDir().mkpath(QFileInfo(AdmissionProject::columnsPath(region)).absolutePath());
		QFile file(AdmissionProject::columnsPath(region));

		if (! file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			if (error)
				*error = QObject::tr("Cannot write %1").arg(AdmissionProject::columnsPath(region));
			return false;
		}

		file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
		file.close();
		return true;
	}

	QString preview(const AdmissionColumnRule &rule, const AdmissionNamingContext &context, QString *error) {
		if (rule.type == QStringLiteral("random"))
			return freshRandom(rule);

		return AdmissionNaming::render(rule.templateText, context, rule.numberSources, rule.charSources,
		                               error);
	}

	bool evaluate(const QList<AdmissionColumnRule> &rules, AdmissionTable &table, const QString &section,
	              bool onlyEmpty, QString *error) {
		table.normalize();

		for (const AdmissionColumnRule &rule : rules) {
			int column = table.columnIndex(rule.name);

			if (column < 0) {
				table.header << rule.name;

				for (QStringList &row : table.rows)
					row << QString();

				column = table.header.size() - 1;
			}

			if (rule.type == QStringLiteral("template")) {
				QString renderError;

				if (! AdmissionNaming::validate(rule.templateText, table.header, &renderError)) {
					if (error)
						*error = QObject::tr("Column %1: %2").arg(rule.name, renderError);
					return false;
				}

				// 引用必须指向「已经求值」的列：按 CSV 列从左到右处理。
				const int nameColumn = table.columnIndex(QStringLiteral("姓名"));
				const int idColumn = table.columnIndex(QStringLiteral("准考证号"));
				const int seatColumn = table.columnIndex(QStringLiteral("座位号"));
				const int roomColumn = table.columnIndex(QStringLiteral("考场"));

				for (int row = 0; row < table.rows.size(); ++row) {
					AdmissionNamingContext context;
					context.section = section;
					context.row = row + 1;
					context.col = column + 1;
					context.regionSeq = row + 1;
					context.globalSeq = row + 1;
					context.name = table.cell(row, nameColumn);
					context.id = table.cell(row, idColumn);
					context.seat = table.cell(row, seatColumn);
					context.room = table.cell(row, roomColumn);

					for (int index = 0; index < table.header.size(); ++index)
						if (index < column)
							context.columns.insert(table.header.at(index), table.cell(row, index));

					QString renderError;

					if (! AdmissionNaming::validate(rule.templateText, table.header, &renderError)) {
						if (error)
							*error = QObject::tr("Column %1: %2").arg(rule.name, renderError);
						return false;
					}

					const QString value = AdmissionNaming::render(rule.templateText, context,
					                                              rule.numberSources, rule.charSources,
					                                              &renderError);

					if (! renderError.isEmpty()) {
						if (error)
							*error = QObject::tr("Column %1: %2").arg(rule.name, renderError);
						return false;
					}

					// 模板列总是按列序重算（源列变则变）；onlyEmpty 只对随机列生效。
					table.rows[row][column] = value;
				}

				continue;
			}

			// 随机列：只填空值；可重算的按种子，同一行永远一样。
			const int idColumn = table.columnIndex(QStringLiteral("准考证号"));
			const int nameColumn = table.columnIndex(QStringLiteral("姓名"));

			for (int row = 0; row < table.rows.size(); ++row) {
				if (onlyEmpty && ! table.cell(row, column).isEmpty())
					continue;

				QString value;

				if (rule.recalculable) {
					QString seed = QString::number(row + 1) + section + rule.name;

					if (rule.seedSource == QStringLiteral("id"))
						seed = table.cell(row, idColumn) + rule.name;
					else if (rule.seedSource == QStringLiteral("name"))
						seed = table.cell(row, nameColumn) + rule.name;

					value = seededRandom(seed, rule);
				} else {
					value = freshRandom(rule);
				}

				table.rows[row][column] = value;
			}
		}

		return true;
	}
} // namespace AdmissionColumns
