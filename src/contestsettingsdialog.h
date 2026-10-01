/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include <QDialog>

class Contest;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

/**
 * 比赛日设置：赛区、违规检测与选手文件夹命名限制。
 *
 * 语义与 gengen-tuack 的「比赛日设置」一致，界面沿用 LemonLime 的风格（确认后才写回）。
 */
class ContestSettingsDialog : public QDialog {
	Q_OBJECT
  public:
	explicit ContestSettingsDialog(QWidget *parent = nullptr);

	/// 把比赛对象当前的设置读进界面。
	void resetEditContest(Contest *contest);
	/// 把界面上的设置写回比赛对象。
	void applyTo(Contest *contest) const;

  private slots:
	void addRule();
	void removeRule();
	void restoreDefaults();
	void updateNamingHint();

  private:
	QCheckBox *regionCheck{};
	QCheckBox *violationCheck{};
	QTableWidget *rulesTable{};
	QCheckBox *namingCheck{};
	QLineEdit *namingPatternEdit{};
	QLabel *namingHint{};
	QString sampleRegion;
};
