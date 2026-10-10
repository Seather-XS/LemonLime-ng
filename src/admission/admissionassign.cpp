/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "admissionassign.h"
//
#include "admissionnaming.h"
//
#include <QMap>
#include <QObject>
#include <QRandomGenerator>

namespace AdmissionAssign {
	namespace {
		/// 摊到座位时的「一格」：一个考场 + 分给它的名额。
		struct Slot {
			QString venue;
			QString room;
			int capacity{1};
			int count{0};
		};
	} // namespace

	bool planSeats(const AdmissionTable &table, const QList<AdmissionVenue> &venues, const SeatOptions &options,
	               QStringList &venueValues, QStringList &roomValues, QStringList &seatValues, QString *error) {
		venueValues.clear();
		roomValues.clear();
		seatValues.clear();

		const int count = table.rows.size();

		if (count == 0)
			return true;

		QList<Slot> seatSlots;

		for (const AdmissionVenue &venue : venues)
			for (const AdmissionRoom &room : venue.rooms)
				if (! room.name.trimmed().isEmpty())
					seatSlots << Slot{venue.name.trimmed(), room.name.trimmed(), qMax(1, room.capacity), 0};

		if (seatSlots.isEmpty()) {
			if (error)
				*error = QObject::tr("No venue / room plan yet. Add venues and rooms first.");

			return false;
		}

		int capacity = 0;

		for (const Slot &slot : seatSlots)
			capacity += slot.capacity;

		if (count > capacity) {
			if (error)
				*error = QObject::tr("%1 contestants but the rooms hold only %2.").arg(count).arg(capacity);

			return false;
		}

		// 每个考场分多少人
		if (options.layout == Balanced) {
			const int base = count / seatSlots.size();
			int assigned = 0;

			for (Slot &slot : seatSlots) {
				slot.count = qMin(slot.capacity, base);
				assigned += slot.count;
			}

			int leftover = count - assigned;

			while (leftover > 0) {
				bool progressed = false;

				for (Slot &slot : seatSlots) {
					if (leftover == 0)
						break;

					if (slot.count < slot.capacity) {
						++slot.count;
						--leftover;
						progressed = true;
					}
				}

				if (! progressed)
					break;
			}
		} else {
			int remaining = count;

			for (Slot &slot : seatSlots) {
				slot.count = qMin(slot.capacity, remaining);
				remaining -= slot.count;

				if (remaining == 0)
					break;
			}
		}

		// 座位号的位数：全赛区人数最多的那个考场说了算（各考点 / 考场互通）
		int width = 1;

		for (const Slot &slot : seatSlots)
			width = qMax(width, QString::number(slot.count).size());

		// 处理顺序：名单行号（默认）或随机
		QList<int> pending;

		for (int row = 0; row < count; ++row)
			pending << row;

		if (options.order == QStringLiteral("random")) {
			// 随机排座不需要可复现的种子
			for (int i = pending.size() - 1; i > 0; --i)
				pending.swapItemsAt(i, int(QRandomGenerator::global()->bounded(i + 1)));
		}

		venueValues = QStringList(count, QString());
		roomValues = QStringList(count, QString());
		seatValues = QStringList(count, QString());

		for (const Slot &slot : seatSlots)
			for (int seat = 0; seat < slot.count && ! pending.isEmpty(); ++seat) {
				const int row = pending.takeFirst();
				venueValues[row] = slot.venue;
				roomValues[row] = slot.room;
				seatValues[row] = QString::number(seat + 1).rightJustified(width, QChar('0'));
			}

		return true;
	}

	bool planIds(const AdmissionTable &table, const QString &section, const IdOptions &options,
	             QStringList &ids, QString *error) {
		ids.clear();

		QString validation;

		if (! AdmissionNaming::validate(options.templateText, table.header, &validation)) {
			if (error)
				*error = validation;
			return false;
		}

		const int idColumn = table.columnIndex(QStringLiteral("准考证号"));
		const int nameColumn = table.columnIndex(QStringLiteral("姓名"));
		const int seatColumn = table.columnIndex(QStringLiteral("座位号"));
		const int roomColumn = table.columnIndex(QStringLiteral("考场"));

		// 数字一律取自名单行号（用户要求：其它来源基本没用）；位数由 <number> 的个数决定。
		QStringList numberSources;
		const int numberGroups =
		    AdmissionNaming::groupCount(options.templateText, QStringLiteral("number"));

		for (int group = 0; group < numberGroups; ++group)
			numberSources << QStringLiteral("row");

		for (int row = 0; row < table.rows.size(); ++row) {
			AdmissionNamingContext context;
			context.section = section;
			context.row = row + 1;
			context.regionSeq = row + 1; // 每个赛区独立编号、按 CSV 行序
			context.globalSeq = options.globalOffset + row + 1;
			context.name = table.cell(row, nameColumn);
			context.id = table.cell(row, idColumn);
			context.seat = table.cell(row, seatColumn);
			context.room = table.cell(row, roomColumn);

			for (int index = 0; index < table.header.size(); ++index)
				context.columns.insert(table.header.at(index), table.cell(row, index));

			QString renderError;
			const QString value = AdmissionNaming::render(options.templateText, context, numberSources,
			                                              options.charSources, &renderError);

			if (! renderError.isEmpty()) {
				if (error)
					*error = renderError;
				return false;
			}

			ids << value;
		}

		return true;
	}

	void applyColumn(AdmissionTable &table, const QString &column, const QStringList &values) {
		int index = table.columnIndex(column);

		if (index < 0) {
			table.header << column;

			for (QStringList &row : table.rows)
				row << QString();

			index = table.header.size() - 1;
		}

		for (int row = 0; row < table.rows.size() && row < values.size(); ++row) {
			while (table.rows[row].size() <= index)
				table.rows[row] << QString();

			table.rows[row][index] = values.at(row);
		}
	}
} // namespace AdmissionAssign
