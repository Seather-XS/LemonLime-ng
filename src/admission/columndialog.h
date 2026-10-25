/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "admissioncolumns.h"
#include "admissionnaming.h"
#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class ColumnDialog : public QDialog {
	Q_OBJECT

  public:
	ColumnDialog(const QString &column, const QStringList &header, const AdmissionNamingContext &sample,
	             const AdmissionColumnRule *existing = nullptr, QWidget *parent = nullptr);

	AdmissionColumnRule rule() const { return result; }

  private slots:
	void updateState();
	void acceptClicked();

  private:
	void buildUi();
	void load();
	void retranslate();

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
