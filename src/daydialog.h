/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include "core/dayproject.h"

#include <QDialog>
#include <QList>

namespace Ui {
	class DayDialog;
}

/**
 * 比赛日窗口：在「打开」页签选择已有比赛日，或在「新建」页签新建一个。
 *
 * 与 gengen-tuack 的「选定比赛后会再弹出一次同样的窗口用于选择或新建比赛日」一致，
 * 界面上与「新建比赛」保持一致（标题 / 名称 / 位置），但**位置只读**：
 * 比赛日目录固定建在比赛目录内，用户只能输入目录名。
 */
class DayDialog : public QDialog {
	Q_OBJECT
  public:
	explicit DayDialog(QWidget *parent = nullptr);
	~DayDialog();

	/// 填入比赛目录与已有比赛日（currentIndex 为当前所在比赛日，-1 表示还没有）。
	void setContext(const QString &contestDir, const QList<DayEntry> &days, int currentIndex);
	/// 打开时直接切到「新建」页签。
	void preferNewTab();

	bool isNewDay() const;
	/// 用户在「打开」页签点了删除（确认后）——由调用方真正删除该比赛日。
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
