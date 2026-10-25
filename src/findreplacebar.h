/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QTextDocument>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class FindReplaceBar : public QWidget {
	Q_OBJECT

  public:
	explicit FindReplaceBar(QWidget *parent = nullptr);

	void setEditor(QPlainTextEdit *editor);
	void activate(bool focusReplace = false);
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
