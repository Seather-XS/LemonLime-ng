/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "admissioncsv.h"
//
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QStringConverter>
#include <string>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
	/// 把 GBK / GB18030 的字节解成 QString（导入国内系统导出的名单时很常见）。
	bool decodeGbk(const QByteArray &raw, QString &text) {
		text.clear();

		if (raw.isEmpty())
			return true;

		if (raw.contains('\0'))
			return false;

#ifdef Q_OS_WIN
		const int length = MultiByteToWideChar(936, 0, raw.constData(), raw.size(), nullptr, 0);

		if (length <= 0)
			return false;

		std::wstring buffer(static_cast<size_t>(length), L'\0');

		if (MultiByteToWideChar(936, 0, raw.constData(), raw.size(), buffer.data(), length) != length)
			return false;

		text = QString::fromWCharArray(buffer.data(), length);
		return true;
#else
		const std::optional<QStringConverter::Encoding> encoding =
		    QStringConverter::encodingForName("GB18030");

		if (! encoding.has_value())
			return false;

		QStringDecoder decoder(encoding.value());

		if (! decoder.isValid())
			return false;

		text = decoder(raw);
		return ! decoder.hasError();
#endif
	}
} // namespace

auto AdmissionTable::builtinColumns() -> QStringList {
	return {QStringLiteral("姓名"),   QStringLiteral("准考证号"), QStringLiteral("考点"),
	        QStringLiteral("考场"),   QStringLiteral("座位号"),   QStringLiteral("照片")};
}

void AdmissionTable::normalize() {
	if (header.isEmpty())
		header = builtinColumns();

	// 内置列（姓名 / 准考证号 / 考点 / 考场 / 座位号 / 照片）固定在前面且总是存在；
	// 单元格跟着列名一起搬，所以既不会错位，也不会丢数据。
	QStringList wanted = builtinColumns();

	for (const QString &name : header)
		if (! wanted.contains(name))
			wanted << name;

	for (QStringList &row : rows) {
		QStringList moved;

		for (const QString &name : wanted) {
			const int index = header.indexOf(name);
			moved << ((index >= 0 && index < row.size()) ? row.at(index) : QString());
		}

		row = moved;
	}

	header = wanted;
}

auto AdmissionTable::columnIndex(const QString &name) const -> int { return header.indexOf(name); }

auto AdmissionTable::cell(int row, int column) const -> QString {
	const QStringList &line = rows.value(row);
	return (column >= 0 && column < line.size()) ? line.at(column) : QString();
}

auto AdmissionTable::rowAt(int index) const -> QStringList {
	QStringList row = rows.value(index);

	while (row.size() < header.size())
		row << QString();

	return row;
}

namespace AdmissionCsv {
	QString escape(const QString &value) {
		QString out = value;

		if (out.contains(QChar(',')) || out.contains(QChar('"')) || out.contains(QChar('\n')))
			out = QChar('"') + out.replace(QChar('"'), QStringLiteral("\"\"")) + QChar('"');

		return out;
	}

	bool looksLikeGbk(const QByteArray &raw) {
		// 没有 BOM、又解不出合法 UTF-8 的，基本就是 GBK 这类本地编码。
		if (raw.startsWith("\xEF\xBB\xBF"))
			return false;

		return ! QString::fromUtf8(raw).toUtf8().size() || QString::fromUtf8(raw).contains(QChar(0xFFFD));
	}

	bool parse(const QString &text, AdmissionTable &table, QString *error) {
		QList<QStringList> records;
		QStringList record;
		QString field;
		bool inQuotes = false;
		const QString source = text.startsWith(QChar(0xFEFF)) ? text.mid(1) : text;

		for (int i = 0; i < source.length(); ++i) {
			const QChar ch = source.at(i);

			if (inQuotes) {
				if (ch == QChar('"')) {
					if (i + 1 < source.length() && source.at(i + 1) == QChar('"')) {
						field += QChar('"');
						++i;
					} else {
						inQuotes = false;
					}
				} else {
					field += ch;
				}

				continue;
			}

			if (ch == QChar('"')) {
				inQuotes = true;
			} else if (ch == QChar(',')) {
				record << field;
				field.clear();
			} else if (ch == QChar('\n')) {
				record << field;
				field.clear();
				records << record;
				record.clear();
			} else if (ch != QChar('\r')) {
				field += ch;
			}
		}

		if (! field.isEmpty() || ! record.isEmpty()) {
			record << field;
			records << record;
		}

		QList<QStringList> kept;

		for (const QStringList &line : records) {
			bool empty = true;
			QStringList trimmed;

			for (const QString &cell : line) {
				const QString value = cell.trimmed();
				trimmed << value;

				if (! value.isEmpty())
					empty = false;
			}

			if (empty || trimmed.first().startsWith(QChar('#')))
				continue;

			kept << trimmed;
		}

		if (kept.isEmpty()) {
			if (error)
				*error = QObject::tr("The list is empty.");
			return false;
		}

		table.header = kept.first();
		table.rows.clear();

		for (int i = 1; i < kept.size(); ++i)
			table.rows << kept.at(i);

		table.normalize();
		return true;
	}

	QString write(const AdmissionTable &table) {
		AdmissionTable copy = table;
		copy.normalize();
		QStringList lines;
		QStringList head;

		for (const QString &name : copy.header)
			head << escape(name);

		lines << head.join(QChar(','));

		for (const QStringList &row : copy.rows) {
			bool empty = true;
			QStringList cells;

			for (const QString &value : row) {
				if (! value.trimmed().isEmpty())
					empty = false;

				cells << escape(value.trimmed());
			}

			if (! empty)
				lines << cells.join(QChar(','));
		}

		return lines.join(QChar('\n')) + QChar('\n');
	}

	bool readFile(const QString &path, AdmissionTable &table, QString *error) {
		QFile file(path);

		if (! file.open(QIODevice::ReadOnly)) {
			if (error)
				*error = QObject::tr("Cannot read %1").arg(path);
			return false;
		}

		const QByteArray raw = file.readAll();
		file.close();

		QString text = QString::fromUtf8(raw);

		if (looksLikeGbk(raw)) {
			// 国内系统导出的 CSV 基本都是 GBK，直接转过来，不用让用户自己另存。
			QString decoded;

			if (! decodeGbk(raw, decoded)) {
				if (error)
					*error = QObject::tr("%1 is not UTF-8; please save it as UTF-8 CSV.")
				             .arg(QFileInfo(path).fileName());
				return false;
			}

			text = decoded;
		}

		return parse(text, table, error);
	}

	bool writeFile(const QString &path, const AdmissionTable &table, QString *error) {
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile file(path);

		if (! file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			if (error)
				*error = QObject::tr("Cannot write %1").arg(path);
			return false;
		}

		file.write(QString(QChar(0xFEFF)).toUtf8());
		file.write(write(table).toUtf8());
		file.close();
		return true;
	}

	bool writeTemplate(const QString &path, const QStringList &header, QString *error) {
		AdmissionTable table;
		table.header = header.isEmpty() ? AdmissionTable::builtinColumns() : header;
		return writeFile(path, table, error);
	}
} // namespace AdmissionCsv
