/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QTextDocument>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

/**
 * 编辑器用的查找 / 替换栏（QPlainTextEdit 自身不带这个界面）。
 *
 * Ctrl+F 打开查找、Ctrl+H 打开查找+替换，Esc 关闭；Enter / Shift+Enter 上下查找，
 * 查找默认回绕，可选区分大小写。
 */
class FindReplaceBar : public QWidget {
	Q_OBJECT

  public:
	explicit FindReplaceBar(QWidget *parent = nullptr);

	void setEditor(QPlainTextEdit *editor);
	/// 显示并聚焦；`focusReplace` 为真时聚焦替换框（Ctrl+H）。
	void activate(bool focusReplace = false);
	/// 隐藏并让焦点回到编辑器。
	void deactivate();

  protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

  private:
	void find(bool forward);
	void replaceCurrent();
	void replaceAll();
	void setStatus(const QString &text);
	QTextDocument::FindFlags findFlags() const;

	QPlainTextEdit *editor{};
	QLineEdit *findEdit{};
	QLineEdit *replaceEdit{};
	QCheckBox *caseCheck{};
	QCheckBox *wholeWordCheck{};
	QLabel *statusLabel{};
	QPushButton *replaceButton{};
	QPushButton *replaceAllButton{};
};
