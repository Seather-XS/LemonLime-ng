/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QDialog>
#include <QString>

class QPlainTextEdit;

/**
 * 一段通告（注意事项 / 比赛注意）的编辑框：纯文本，只额外认 [文字](链接) 超链接。
 */
class NotesDialog : public QDialog {
	Q_OBJECT

  public:
	NotesDialog(const QString &title, const QString &text, QWidget *parent = nullptr);

	/// 确定之后的文本。
	QString text() const;

  private:
	QPlainTextEdit *editor{};
};
