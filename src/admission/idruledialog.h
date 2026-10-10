/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "admissionassign.h"
//
#include <QDialog>
#include <QList>
#include <QString>

class QComboBox;
class QFormLayout;
class QLineEdit;
class QPlainTextEdit;
class QDialogButtonBox;

/**
 * 准考证号策略：模板。
 *
 * 数字一律取自**名单行号**（其它来源基本没人用，用户要求直接强制）；模板里 `<number>` 的
 * 个数就是位数（`<number><number><number>` = 001、002…）。模板里带 `<char>` 时，下面才出现
 * 「字母取自」的下拉。每次「生成准考证号」都走这个对话框，确定后由调用方写回 config.json
 * 的 idRule；号码一律覆盖已有的。预览放在只读文本框里（能滚动，人数多了也不会把面板撑爆）。
 */
class IdRuleDialog : public QDialog {
	Q_OBJECT

  public:
	IdRuleDialog(const AdmissionTable &table, const QString &section,
	             const AdmissionAssign::IdOptions &current, QWidget *parent = nullptr);

	/// 用户确定后的策略。
	AdmissionAssign::IdOptions options() const;

  private slots:
	void updateState();

  private:
	/// 按模板里的字母组数补 / 减下拉（组数没变就不动，避免在控件自己的信号里删掉它）。
	void syncGroups();
	void updatePreview();
	QComboBox *makeSourceBox(const QString &key);
	const AdmissionTable &table;
	QString section;

	QLineEdit *templateEdit{};
	QFormLayout *groupsForm{};
	QPlainTextEdit *preview{};
	QDialogButtonBox *buttons{};

	QList<QComboBox *> charBoxes;
	/// 对话框打开时字母组的来源（重建下拉时用来兜底）。
	QStringList initialCharSources;
};
