/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include "core/dayproject.h"

#include <QDialog>
#include <QList>

namespace Ui {
	class DayDialog;
}

class DayDialog : public QDialog {
	Q_OBJECT
  public:
	explicit DayDialog(QWidget *parent = nullptr);
	~DayDialog();

	void setContext(const QString &contestDir, const QList<DayEntry> &days, int currentIndex);
	void preferNewTab();

	bool isNewDay() const;
	bool isDeleteRequested() const;
	int getSelectedIndex() const;
	QString getDayTitle() const;
	QString getDayFileName() const;

  private slots:
	void updateOk();
	void syncFolderFromTitle();
	void updatePathPreview();
	void deleteSelected();

  private:
	Ui::DayDialog *ui;
	QString contestDir;
	bool folderEdited{false};
	bool deleteRequested{false};
};
