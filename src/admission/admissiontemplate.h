/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace AdmissionTemplate {
	QString root();
	QString filePath();
	QString source();
	QString fontDir();
	QStringList lockedRowLabels();
	bool validate(const QString &text, QString *error);
	QString render(const QString &text, const QMap<QString, QString> &values, QStringList *unresolved);
} 
