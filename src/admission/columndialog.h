/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "admissioncolumns.h"
#include "admissionnaming.h"
//
#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;

/**
 * 一列的生成规则编辑器：模板（占位符拼出来）或随机（长度 + 字符集 + 是否可重算）。
 *
 * 只改内存里的规则，写盘交给 CsvEditorDialog（columns.json）。
 */
class ColumnDialog : public QDialog {
	Q_OBJECT

  public:
	ColumnDialog(const QString &column, const QStringList &header, const AdmissionNamingContext &sample,
	             const AdmissionColumnRule *existing = nullptr, QWidget *parent = nullptr);

	/// 点了确定之后的规则。
	AdmissionColumnRule rule() const { return result; }

  private slots:
	void updateState();
	void acceptClicked();

  private:
	void buildUi();
	void load();
	void retranslate();

	/// 按界面上的选项拼一条规则。
	AdmissionColumnRule collect() const;

	QString column;
	QStringList header;
	AdmissionNamingContext sample;
	const AdmissionColumnRule *existingRule{};

	AdmissionColumnRule result;

	QLabel *columnValue{};
	QLabel *typeLabel{};
	QComboBox *typeBox{};
	QWidget *templateBox{};
	QLabel *templateLabel{};
	QLineEdit *templateEdit{};
	QWidget *randomBox{};
	QLabel *lengthLabel{};
	QSpinBox *lengthBox{};
	QLabel *charsetLabel{};
	QCheckBox *upperBox{};
	QCheckBox *lowerBox{};
	QCheckBox *digitBox{};
	QCheckBox *symbolBox{};
	QCheckBox *ambiguousBox{};
	QCheckBox *recalculableBox{};
	QLabel *sourceLabel{};
	QComboBox *seedBox{};
	QLabel *previewText{};
	QDialogButtonBox *buttons{};
};
