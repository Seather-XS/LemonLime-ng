/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "notesdialog.h"
//
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QVBoxLayout>

NotesDialog::NotesDialog(const QString &title, const QString &text, QWidget *parent) : QDialog(parent) {
	setWindowTitle(title);

	auto *layout = new QVBoxLayout(this);
	editor = new QPlainTextEdit(this);
	editor->setPlainText(text);
	editor->setMinimumSize(560, 360);
	editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	layout->addWidget(editor);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	editor->setFocus();
}

auto NotesDialog::text() const -> QString { return editor->toPlainText(); }
