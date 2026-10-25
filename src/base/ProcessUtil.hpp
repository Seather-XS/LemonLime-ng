/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
#include <QProcess>

namespace Lemon::common {
	inline void suppressConsoleWindow(QProcess &process) {
#ifdef Q_OS_WIN32
		process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
			args->flags |= 0x08000000UL;
		});
#else
		Q_UNUSED(process);
#endif
	}
}
