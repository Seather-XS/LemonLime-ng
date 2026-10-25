/*
 * SPDX-FileCopyrightText: 2019-2020  Qv2ray Development Group
 *                         2020-2022  Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
#include <QString>
#include <QTranslator>
#include <memory>
#include <optional>

namespace Lemon::common {
	class LemonTranslator {
	  public:
		explicit LemonTranslator();

	  public:
		const inline QStringList GetAvailableLanguages() const { return languages; }
		bool InstallTranslation(const QString &);

	  private:
		QStringList languages;
		QStringList searchPaths;
		std::unique_ptr<QTranslator> pTranslator;
		void refreshTranslations();
	};
	inline std::unique_ptr<common::LemonTranslator> LemonLimeTranslator;
} 

using namespace Lemon::common;
