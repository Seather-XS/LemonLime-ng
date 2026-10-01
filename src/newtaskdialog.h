/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include <QDialog>

class QLineEdit;
class QPushButton;
class QSpinBox;

/**
 * 新建试题的小窗口：显示标题 / 文件名 / 时间限制 / 空间限制。
 *
 * 文件名默认跟随标题自动生成，用户也可以在窗口里单独改；但**试题创建完成后**该文件名
 * 即固定（`TaskEditWidget` 中的对应输入框为只读）。
 *
 * 语义参考 gengen-tuack 的试题配置：`time limit` / `memory limit` 是**试题级**的默认值，
 * 新建测试点时沿用（测试点自身仍可覆盖）。
 */
class NewTaskDialog : public QDialog {
	Q_OBJECT
  public:
	explicit NewTaskDialog(QWidget *parent = nullptr);

	/// 设定时间/空间限制的初值（一般传设置里的默认值）。
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
	bool fileEdited{false}; ///< 用户手动改过文件名后，标题不再覆盖它
};
