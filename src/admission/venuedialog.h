/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "admissionproject.h"
#include <QDialog>
#include <QList>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;

class VenueDialog : public QDialog {
	Q_OBJECT

  public:
	VenueDialog(const QString &region, const QList<AdmissionVenue> &venues, QWidget *parent = nullptr);

	QList<AdmissionVenue> venues() const;

  private slots:
	void addVenue();
	void addRoom();
	void removeSelected();
	void itemChanged(QTreeWidgetItem *item, int column);
	void acceptClicked();

  private:
	QTreeWidgetItem *selectedVenue() const;

	QString regionName;
	QTreeWidget *tree{};
	bool updating{false};
};
