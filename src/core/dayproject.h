/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

struct DayEntry {
	QString title;
	QString file;
};

class DayProject {
  public:
	QString title;
	QList<DayEntry> days;

	static bool isProjectObject(const QJsonObject &obj);
	static DayProject fromJson(const QJsonObject &obj);
	QJsonObject toJson() const;

	QString dayPath(const QString &root, int index) const;
};
