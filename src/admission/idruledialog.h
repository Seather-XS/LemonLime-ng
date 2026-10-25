/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "admissionassign.h"
#include <QDialog>
#include <QList>
#include <QString>

class QComboBox;
class QFormLayout;
class QLineEdit;
class QPlainTextEdit;
class QDialogButtonBox;

class IdRuleDialog : public QDialog {
	Q_OBJECT

  public:
	IdRuleDialog(const AdmissionTable &table, const QString &section,
	             const AdmissionAssign::IdOptions &current, QWidget *parent = nullptr);

	AdmissionAssign::IdOptions options() const;

  private slots:
	void updateState();

  private:
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
	QStringList initialCharSources;
};
