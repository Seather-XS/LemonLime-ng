/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "dayproject.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonValue>

bool DayProject::isProjectObject(const QJsonObject &obj) {
	return obj.contains(QStringLiteral("days")) && obj.value(QStringLiteral("days")).isArray();
}

auto DayProject::fromJson(const QJsonObject &obj) -> DayProject {
	DayProject project;
	project.title = obj.value(QStringLiteral("title")).toString();

	for (const auto &value : obj.value(QStringLiteral("days")).toArray()) {
		const QJsonObject item = value.toObject();
		DayEntry entry;
		entry.title = item.value(QStringLiteral("title")).toString();
		entry.file = item.value(QStringLiteral("file")).toString();

		if (! entry.file.isEmpty())
			project.days.append(entry);
	}

	return project;
}

auto DayProject::toJson() const -> QJsonObject {
	QJsonObject obj;
	obj.insert(QStringLiteral("version"), QStringLiteral("1.0"));
	obj.insert(QStringLiteral("title"), title);

	QJsonArray array;

	for (const DayEntry &entry : days) {
		QJsonObject item;
		item.insert(QStringLiteral("title"), entry.title);
		item.insert(QStringLiteral("file"), entry.file);
		array.append(item);
	}

	obj.insert(QStringLiteral("days"), array);
	return obj;
}

auto DayProject::dayPath(const QString &root, int index) const -> QString {
	if (index < 0 || index >= days.size())
		return QString();

	return QDir(root).absoluteFilePath(days.at(index).file);
}
