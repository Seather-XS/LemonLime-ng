/*
 * SPDX-FileCopyrightText: 2020-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "LemonUtils.hpp"

namespace Lemon::common {

	auto GetFileList(const QDir &dir) -> QStringList {
		return dir.entryList(QStringList{"*", "*.*"}, QDir::Hidden | QDir::Files);
	}

	auto FileExistsIn(const QDir &dir, const QString &fileName) -> bool {
		return GetFileList(dir).contains(fileName);
	}

	QString FileNameSafePart(const QString &name, const QString &fallback) {
		QString result = name.trimmed();
		static const QString forbidden = QStringLiteral("\\/:*?\"<>|");

		for (const QChar bad : forbidden)
			result.replace(bad, QChar('_'));

		result = result.trimmed();

		while (result.endsWith(QChar('.')))
			result.chop(1);

		return result.isEmpty() ? fallback : result;
	}

	QString ResolveStatementPdfName(const QString &pattern, const QString &dayName,
	                                const QString &dayTitle, const QString &contestTitle,
	                                const QString &fallback) {
		QString result = pattern.trimmed();

		if (result.isEmpty())
			return fallback;

		const QString dayValue = dayName.trimmed().isEmpty() ? fallback : dayName.trimmed();
		const QString titleDayValue = dayTitle.trimmed().isEmpty() ? dayValue : dayTitle.trimmed();
		const QString titleValue = contestTitle.trimmed().isEmpty() ? titleDayValue : contestTitle.trimmed();

		for (const QString &key : {QStringLiteral("<title-day>"), QStringLiteral("<title_day>")})
			result.replace(key, titleDayValue, Qt::CaseInsensitive);

		result.replace(QStringLiteral("<day>"), dayValue, Qt::CaseInsensitive);
		result.replace(QStringLiteral("<title>"), titleValue, Qt::CaseInsensitive);

		result.replace(QChar('/'), QChar('_'));
		result.replace(QChar('\\'), QChar('_'));
		return FileNameSafePart(result, fallback);
	}

}
