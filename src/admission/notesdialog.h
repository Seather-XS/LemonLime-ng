/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;

class NotesDialog : public QDialog {
	Q_OBJECT

  public:
	NotesDialog(const QString &title, const QString &text, QWidget *parent = nullptr);

	QString text() const;

  private:
	QPlainTextEdit *editor{};
};
