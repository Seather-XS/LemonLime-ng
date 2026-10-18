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
class Settings;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;

/**
 * 比赛日设置：赛区、违规检测与选手文件夹命名限制。
 *
 * 语义与 gengen-tuack 的「比赛日设置」一致，界面沿用 LemonLime 的风格（确认后才写回）。
 */
class ContestSettingsDialog : public QDialog {
	Q_OBJECT
  public:
	explicit ContestSettingsDialog(QWidget *parent = nullptr);

	/// 违规检测按语言分开：这里要从全局设置里取编译器列表。
	void setSettings(Settings *);
	/// 把比赛对象当前的设置读进界面。
	void resetEditContest(Contest *contest);
	/// 把界面上的设置写回比赛对象。
	void applyTo(Contest *contest) const;

  private slots:
	void updateNamingHint();
	void updateViolationState();

  private:
	QCheckBox *regionCheck{};
	QCheckBox *violationCheck{};
	QListWidget *compilerList{};
	QLabel *violationHint{};
	QCheckBox *namingCheck{};
	QLineEdit *namingPatternEdit{};
	QLabel *namingHint{};
	QString sampleRegion;
	Settings *settings{};
};
