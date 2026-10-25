/*
 * SPDX-FileCopyrightText: 2026 gengen-tuack
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <QString>

class SpecialJudge {
  public:
	static QString resolve(const QString &checkerPath, const QString &compilerLocation,
	                       const QString &compileTemplate, int timeLimitMs, QString &error);
	static void cleanup();
};
