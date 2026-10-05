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
class QLabel;
class QLineEdit;
class QDialogButtonBox;

/**
 * 准考证号策略：模板 + 每个数字 / 字母组的取值来源 + 已有号码怎么处理。
 *
 * 每次「生成准考证号」都走这个对话框，用户自己决定怎么编；确定后由调用方写回
 * config.json 的 idRule（策略可以持久化，下次打开还是这套）。
 * 对话框里实时预览前几个号码，模板不合法就直接把错误显示出来。
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
	/// 按模板里的组数补 / 减下拉（组数没变就不动，避免在控件自己的信号里删掉它）。
	void syncGroups();
	void updatePreview();
	QComboBox *makeSourceBox(const QString &key);

	const AdmissionTable &table;
	QString section;

	QLineEdit *templateEdit{};
	QFormLayout *groupsForm{};
	QComboBox *overwriteBox{};
	QLabel *preview{};
	QDialogButtonBox *buttons{};

	QList<QComboBox *> numberBoxes;
	QList<QComboBox *> charBoxes;
	/// 对话框打开时各组的来源（重建下拉时用来兜底）。
	QStringList initialNumberSources;
	QStringList initialCharSources;
};
