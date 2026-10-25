/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "admissioncsv.h"
#include "admissionproject.h"
#include <QList>
#include <QString>
#include <QStringList>

namespace AdmissionAssign {
	enum SeatLayout {
		FillFirst = 0,
		Balanced = 1,
	};

	struct SeatOptions {
		QString order{QStringLiteral("row")};
		int layout{FillFirst};
	};

	struct IdOptions {
		QString templateText;
		QStringList charSources;
		int globalOffset{0};
	};

	bool planSeats(const AdmissionTable &table, const QList<AdmissionVenue> &venues, const SeatOptions &options,
	               QStringList &venueValues, QStringList &roomValues, QStringList &seatValues, QString *error);
	bool planIds(const AdmissionTable &table, const QString &section, const IdOptions &options,
	             QStringList &ids, QString *error);
	void applyColumn(AdmissionTable &table, const QString &column, const QStringList &values);
}
