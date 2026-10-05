/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "admissionproject.h"
//
#include <QDialog>
#include <QList>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;

/**
 * 一个赛区的「考点 / 考场」编辑区：考点是集合，考场是考点里的元素。
 *
 * 每个考场都要写容量上限（至少 1 人）；排座位就以此为唯一根据。
 * 打开时传入当前方案，确定后用 venues() 取回新方案（由调用方写进赛区并落盘）。
 */
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
	/// 选中的考点（选中考场时就是它的父考点）。
	QTreeWidgetItem *selectedVenue() const;

	QString regionName;
	QTreeWidget *tree{};
	bool updating{false};
};
