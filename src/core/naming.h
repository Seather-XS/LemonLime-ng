/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <QString>

class Naming {
  public:
	static QString defaultPattern();

	static QStringList parameters();

	static QString toRegexBody(const QString &pattern, const QString &section);

	static bool matches(const QString &name, const QString &section, const QString &pattern);

	static QString sample(const QString &pattern, const QString &section);

	static QString describe(const QString &name, const QString &section, const QString &pattern);

	static QString folderName(const QString &path);
};
