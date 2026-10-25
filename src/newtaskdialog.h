/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;
class QSpinBox;

class NewTaskDialog : public QDialog {
	Q_OBJECT
  public:
	explicit NewTaskDialog(QWidget *parent = nullptr);

	void setDefaults(const QString &suggestedTitle, int timeLimit, int memoryLimit);

	QString getProblemTitle() const;
	QString getSourceFileName() const;
	int getTimeLimit() const;
	int getMemoryLimit() const;

  private slots:
	void syncFileName();
	void markFileEdited();
	void updateOk();

  private:
	QLineEdit *titleEdit{};
	QLineEdit *fileEdit{};
	QSpinBox *timeLimitEdit{};
	QSpinBox *memoryLimitEdit{};
	QPushButton *okButton{};
	bool fileEdited{false}; 
};
