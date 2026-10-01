/*
 * SPDX-FileCopyrightText: 2020-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "LemonUtils.hpp"
//

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

		// Windows 下文件名不能以点结尾
		while (result.endsWith(QChar('.')))
			result.chop(1);

		return result.isEmpty() ? fallback : result;
	}

} // namespace Lemon::common
