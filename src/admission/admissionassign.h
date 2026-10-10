/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "admissioncsv.h"
#include "admissionproject.h"
//
#include <QList>
#include <QString>
#include <QStringList>

/** 批量分配：排座位、生成准考证号（都是先算一份预览，用户确认后再写回名单）。 */
namespace AdmissionAssign {
	/// 座位怎么摊到各考场。
	enum SeatLayout {
		FillFirst = 0, ///< 尽量先填满靠前的考点 / 考场
		Balanced = 1,  ///< 尽量平均分配每个考场人数
	};

	/// 排座位选项。考点 / 考场 / 容量只能来自 AdmissionRegion::venues。
	struct SeatOptions {
		QString order{QStringLiteral("row")}; ///< row / random
		int layout{FillFirst};                ///< SeatLayout
	};

	/// 生成准考证号选项。号码一律覆盖已有值（用户要求：生成它就是为了重编）；
	/// 数字组固定取自名单行号，只有字母组还能选来源。
	struct IdOptions {
		QString templateText;
		QStringList charSources;
		/// 本赛区之前已经有多少人（字母组选「全场序号」时用它把序号接上）。
		int globalOffset{0};
	};

	/// 排座位：按考点 / 考场方案（含容量）分配，返回每行的 考点 / 考场 / 座位号。
	/// 座位号每个考场都从 1 开始，位数 = 全赛区人数最多的那个考场的位数（各考点 / 考场互通）。
	bool planSeats(const AdmissionTable &table, const QList<AdmissionVenue> &venues, const SeatOptions &options,
	               QStringList &venueValues, QStringList &roomValues, QStringList &seatValues, QString *error);
	/// 生成准考证号：返回按名单顺序排好的号。
	bool planIds(const AdmissionTable &table, const QString &section, const IdOptions &options,
	             QStringList &ids, QString *error);
	/// 把一列值写回名单（列不存在时自动加）。
	void applyColumn(AdmissionTable &table, const QString &column, const QStringList &values);
} // namespace AdmissionAssign
