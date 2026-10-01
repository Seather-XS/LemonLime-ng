/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//
#include <QProcess>

namespace Lemon::common {
	/// 让子进程不要弹出控制台窗口。
	///
	/// g++ / diff / attrib 这类控制台程序由 GUI 程序（lemon.exe）拉起时，Windows 会给它们
	/// 分配一个可见的控制台窗口，表现为屏幕上冒出一个黑色终端窗口。用 CREATE_NO_WINDOW
	/// 创建的子进程同样有控制台，只是窗口不显示，而且这个（隐藏）控制台会被它自己的
	/// 子进程继承 —— 比如 g++ 拉起的 cc1plus / as / ld 也不会另开窗口。
	inline void suppressConsoleWindow(QProcess &process) {
#ifdef Q_OS_WIN32
		process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
			// CREATE_NO_WINDOW，直接写数值可以避免引入 <windows.h>
			args->flags |= 0x08000000UL;
		});
#else
		Q_UNUSED(process);
#endif
	}
} // namespace Lemon::common
