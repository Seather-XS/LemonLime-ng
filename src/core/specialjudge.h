/*
 * SPDX-FileCopyrightText: 2026 gengen-tuack
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include <QString>

class SpecialJudge {
  public:
	// checkerPath: absolute path of the configured special judge.
	// compilerLocation/compileTemplate: the compiler and its configuration template
	// ("%s.*" is the source file, "%s" the name without suffix), only used when the
	// configured file is a source file.
	// A source file must include "testlib.h" and is compiled once per
	// (path, modification time, size, compiler, template); anything else is used as is.
	// Returns the executable to run, or an empty string with error filled in.
	static QString resolve(const QString &checkerPath, const QString &compilerLocation,
	                       const QString &compileTemplate, int timeLimitMs, QString &error);
	// Removes the extracted testlib.h and every compiled checker.
	static void cleanup();
};
