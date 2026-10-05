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
class QPlainTextEdit;

/**
 * 排座位：考点 / 考场方案（含容量）是唯一根据，这里只选怎么摊。
 *
 * 两种摊法：尽量先填满靠前的考点 / 考场，或者各考场平均分配。
 * 座位号每个考场从 1 开始，位数由全赛区人数最多的那个考场决定（各考点 / 考场互通）。
 * 对话框里实时预览每个考场分到多少人、以及前几行的结果。
 */
class SeatDialog : public QDialog {
	Q_OBJECT

  public:
	SeatDialog(const AdmissionTable &table, const QList<AdmissionVenue> &venues,
	           const AdmissionAssign::SeatOptions &current, QWidget *parent = nullptr);

	AdmissionAssign::SeatOptions options() const;
	/// 考点 / 考场改过之后刷新预览。
	void setVenues(const QList<AdmissionVenue> &venues);

  signals:
	/// 用户点了「编辑考点 / 考场…」。
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
