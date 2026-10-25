/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <QDialog>

class Contest;
class Settings;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;

class ContestSettingsDialog : public QDialog {
	Q_OBJECT
  public:
	explicit ContestSettingsDialog(QWidget *parent = nullptr);

	void setSettings(Settings *);
	void resetEditContest(Contest *contest);
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
