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
class QPlainTextEdit;

class SeatDialog : public QDialog {
	Q_OBJECT

  public:
	SeatDialog(const AdmissionTable &table, const QList<AdmissionVenue> &venues,
	           const AdmissionAssign::SeatOptions &current, QWidget *parent = nullptr);

	AdmissionAssign::SeatOptions options() const;
	void setVenues(const QList<AdmissionVenue> &venues);

  signals:
	void editVenuesRequested();

  private slots:
	void updatePreview();

  private:
	const AdmissionTable &table;
	QList<AdmissionVenue> venues;

	QComboBox *layoutBox{};
	QComboBox *orderBox{};
	QPlainTextEdit *preview{};
};
