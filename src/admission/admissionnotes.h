/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QString>

namespace AdmissionNotes {
	QString escape(const QString &text);
	QString toLatex(const QString &text);
} 
