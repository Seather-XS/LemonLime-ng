/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "findreplacebar.h"
//
#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

FindReplaceBar::FindReplaceBar(QWidget *parent) : QWidget(parent) {
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(2);

	// ---- 第一行：查找 ----
	auto *findRow = new QHBoxLayout();
	findRow->setContentsMargins(0, 0, 0, 0);
	findRow->addWidget(new QLabel(tr("Find:"), this));
	findEdit = new QLineEdit(this);
	findEdit->setClearButtonEnabled(true);
	findRow->addWidget(findEdit, 1);

	auto *previousButton = new QToolButton(this);
	previousButton->setText(QStringLiteral("\u2191"));
	previousButton->setToolTip(tr("Previous"));
	auto *nextButton = new QToolButton(this);
	nextButton->setText(QStringLiteral("\u2193"));
	nextButton->setToolTip(tr("Next"));
	findRow->addWidget(previousButton);
	findRow->addWidget(nextButton);

	caseCheck = new QCheckBox(tr("Match case"), this);
	findRow->addWidget(caseCheck);
	wholeWordCheck = new QCheckBox(tr("Whole words"), this);
	findRow->addWidget(wholeWordCheck);

	auto *closeButton = new QToolButton(this);
	closeButton->setText(QStringLiteral("\u2715"));
	closeButton->setToolTip(tr("Close"));
	findRow->addWidget(closeButton);
	outer->addLayout(findRow);

	// ---- 第二行：替换（常显，与 statement-editor 一致）----
	auto *replaceRow = new QHBoxLayout();
	replaceRow->setContentsMargins(0, 0, 0, 0);
	replaceRow->addWidget(new QLabel(tr("Replace:"), this));
	replaceEdit = new QLineEdit(this);
	replaceEdit->setClearButtonEnabled(true);
	replaceRow->addWidget(replaceEdit, 1);
	replaceButton = new QPushButton(tr("Replace"), this);
	replaceAllButton = new QPushButton(tr("Replace All"), this);
	replaceRow->addWidget(replaceButton);
	replaceRow->addWidget(replaceAllButton);
	statusLabel = new QLabel(this);
	replaceRow->addWidget(statusLabel);
	outer->addLayout(replaceRow);

	hide();

	connect(findEdit, &QLineEdit::textChanged, this, [this] { setStatus(QString()); });
	connect(findEdit, &QLineEdit::returnPressed, this, [this] { find(true); });
	connect(replaceEdit, &QLineEdit::returnPressed, this, [this] { replaceCurrent(); });
	connect(nextButton, &QToolButton::clicked, this, [this] { find(true); });
	connect(previousButton, &QToolButton::clicked, this, [this] { find(false); });
	connect(replaceButton, &QPushButton::clicked, this, &FindReplaceBar::replaceCurrent);
	connect(replaceAllButton, &QPushButton::clicked, this, &FindReplaceBar::replaceAll);
	connect(closeButton, &QToolButton::clicked, this, &FindReplaceBar::deactivate);

	findEdit->installEventFilter(this);
	replaceEdit->installEventFilter(this);
}

void FindReplaceBar::setEditor(QPlainTextEdit *edit) { editor = edit; }

void FindReplaceBar::activate(bool focusReplace) {
	if (! editor)
		return;

	setStatus(QString());
	show();

	const QString selected = editor->textCursor().selectedText();

	// 编辑器里选中了什么就带进查找框（单行内容才对得上）
	if (! selected.isEmpty() && ! selected.contains(QChar(0x2029)) && ! selected.contains(QChar('\n')))
		findEdit->setText(selected);

	if (focusReplace) {
		replaceEdit->setFocus();
		replaceEdit->selectAll();
	} else {
		findEdit->setFocus();
		findEdit->selectAll();
	}
}

void FindReplaceBar::deactivate() {
	hide();

	if (editor)
		editor->setFocus();
}

bool FindReplaceBar::eventFilter(QObject *watched, QEvent *event) {
	if (event->type() == QEvent::KeyPress && (watched == findEdit || watched == replaceEdit)) {
		auto *key = static_cast<QKeyEvent *>(event);

		if (key->key() == Qt::Key_Escape) {
			deactivate();
			return true;
		}

		// Shift+Enter 往上找
		if (watched == findEdit && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
		    key->modifiers().testFlag(Qt::ShiftModifier)) {
			find(false);
			return true;
		}
	}

	return QWidget::eventFilter(watched, event);
}

QTextDocument::FindFlags FindReplaceBar::findFlags() const {
	QTextDocument::FindFlags flags;

	if (caseCheck->isChecked())
		flags |= QTextDocument::FindCaseSensitively;

	if (wholeWordCheck->isChecked())
		flags |= QTextDocument::FindWholeWords;

	return flags;
}

void FindReplaceBar::setStatus(const QString &text) { statusLabel->setText(text); }

void FindReplaceBar::find(bool forward) {
	if (! editor)
		return;

	const QString text = findEdit->text();

	if (text.isEmpty()) {
		setStatus(tr("Type something to find."));
		return;
	}

	QTextDocument::FindFlags flags = findFlags();

	if (! forward)
		flags |= QTextDocument::FindBackward;

	if (editor->find(text, flags)) {
		setStatus(QString());
		return;
	}

	// 没找到就回绕到另一端再试一次
	QTextCursor cursor = editor->textCursor();
	cursor.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
	editor->setTextCursor(cursor);

	if (editor->find(text, flags))
		setStatus(tr("Wrapped around."));
	else
		setStatus(tr("Not found."));
}

void FindReplaceBar::replaceCurrent() {
	if (! editor)
		return;

	const QString text = findEdit->text();

	if (text.isEmpty())
		return;

	QTextCursor cursor = editor->textCursor();

	// 当前选中的就是要找的内容才替换，否则先找下一个
	if (cursor.hasSelection() && cursor.selectedText() == text) {
		cursor.insertText(replaceEdit->text());
		editor->setTextCursor(cursor);
		setStatus(tr("Replaced 1 occurrence."));
	}

	find(true);
}

void FindReplaceBar::replaceAll() {
	if (! editor)
		return;

	const QString text = findEdit->text();

	if (text.isEmpty())
		return;

	const QTextDocument::FindFlags flags = findFlags();
	const QString replacement = replaceEdit->text();
	QTextCursor cursor(editor->document());
	int count = 0;

	cursor.beginEditBlock();

	while (true) {
		cursor = editor->document()->find(text, cursor, flags);

		if (cursor.isNull())
			break;

		cursor.insertText(replacement);
		count++;

		if (count > 100000)
			break;  // 保险丝
	}

	cursor.endEditBlock();
	editor->setTextCursor(cursor);
	setStatus(tr("Replaced %1 occurrence(s).").arg(count));
}
