/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "contestsettingsdialog.h"

#include "core/contest.h"
#include "core/contestant.h"
#include "core/naming.h"
#include "core/violation.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHeaderView>

ContestSettingsDialog::ContestSettingsDialog(QWidget *parent) : QDialog(parent) {
	setWindowTitle(tr("Contest Day Settings"));
	setModal(true);
	resize(560, 520);

	auto *mainLayout = new QVBoxLayout(this);

	// —— 赛区 ——
	auto *regionGroup = new QGroupBox(tr("Regions"), this);
	auto *regionLayout = new QVBoxLayout(regionGroup);
	regionCheck = new QCheckBox(tr("Enable regions (answers/<region>/<contestant>/)"), regionGroup);
	regionLayout->addWidget(regionCheck);
	mainLayout->addWidget(regionGroup);

	// —— 违规检测 ——
	auto *violationGroup = new QGroupBox(tr("Violation checking"), this);
	auto *violationLayout = new QVBoxLayout(violationGroup);
	violationCheck = new QCheckBox(tr("Enable violation checking"), violationGroup);
	violationLayout->addWidget(violationCheck);

	rulesTable = new QTableWidget(0, 2, violationGroup);
	rulesTable->setHorizontalHeaderLabels({tr("Type"), tr("Content")});
	rulesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	rulesTable->verticalHeader()->setVisible(false);
	violationLayout->addWidget(rulesTable);

	auto *buttonLayout = new QHBoxLayout();
	auto *addButton = new QPushButton(tr("Add"), violationGroup);
	auto *removeButton = new QPushButton(tr("Remove"), violationGroup);
	auto *restoreButton = new QPushButton(tr("Restore Defaults"), violationGroup);
	buttonLayout->addWidget(addButton);
	buttonLayout->addWidget(removeButton);
	buttonLayout->addWidget(restoreButton);
	buttonLayout->addStretch();
	violationLayout->addLayout(buttonLayout);
	mainLayout->addWidget(violationGroup, 1);

	// —— 选手文件夹命名限制 ——
	auto *namingGroup = new QGroupBox(tr("Contestant folder naming restriction"), this);
	auto *namingLayout = new QVBoxLayout(namingGroup);
	namingCheck = new QCheckBox(tr("Enable contestant folder naming restriction"), namingGroup);
	namingLayout->addWidget(namingCheck);
	namingPatternEdit = new QLineEdit(namingGroup);
	namingLayout->addWidget(namingPatternEdit);
	namingHint = new QLabel(namingGroup);
	namingHint->setWordWrap(true);
	namingLayout->addWidget(namingHint);
	mainLayout->addWidget(namingGroup);

	auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	mainLayout->addWidget(buttonBox);

	connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(addButton, &QPushButton::clicked, this, &ContestSettingsDialog::addRule);
	connect(removeButton, &QPushButton::clicked, this, &ContestSettingsDialog::removeRule);
	connect(restoreButton, &QPushButton::clicked, this, &ContestSettingsDialog::restoreDefaults);
	connect(namingPatternEdit, &QLineEdit::textChanged, this, &ContestSettingsDialog::updateNamingHint);
	connect(namingCheck, &QCheckBox::toggled, this, &ContestSettingsDialog::updateNamingHint);
}

void ContestSettingsDialog::resetEditContest(Contest *contest) {
	if (! contest)
		return;

	sampleRegion = QStringLiteral("HN");

	for (auto *contestant : contest->getContestantList()) {
		if (! contestant->getRegion().isEmpty()) {
			sampleRegion = contestant->getRegion();
			break;
		}
	}

	regionCheck->setChecked(contest->getRegionEnabled());
	violationCheck->setChecked(contest->getViolationCheck());
	namingCheck->setChecked(contest->getNamingCheck());
	namingPatternEdit->setText(contest->getNamingPattern());

	rulesTable->setRowCount(0);

	for (const ViolationRule &rule : contest->getViolationRules()) {
		const int row = rulesTable->rowCount();
		rulesTable->insertRow(row);
		auto *combo = new QComboBox(rulesTable);
		combo->addItem(tr("Command"), Violation::kindCommand());
		combo->addItem(tr("String"), Violation::kindString());
		combo->setCurrentIndex(rule.isCommand() ? 0 : 1);
		rulesTable->setCellWidget(row, 0, combo);
		rulesTable->setItem(row, 1, new QTableWidgetItem(rule.text));
	}

	updateNamingHint();
}

void ContestSettingsDialog::applyTo(Contest *contest) const {
	if (! contest)
		return;

	contest->setRegionEnabled(regionCheck->isChecked());
	contest->setViolationCheck(violationCheck->isChecked());

	QVector<ViolationRule> rules;

	for (int row = 0; row < rulesTable->rowCount(); row++) {
		auto *combo = qobject_cast<QComboBox *>(rulesTable->cellWidget(row, 0));
		const QString kind = combo ? combo->currentData().toString() : Violation::kindCommand();
		const QTableWidgetItem *item = rulesTable->item(row, 1);
		const QString text = item ? item->text().trimmed() : QString();

		if (! text.isEmpty())
			rules.append({kind, text});
	}

	contest->setViolationRules(rules);
	contest->setNamingCheck(namingCheck->isChecked());
	contest->setNamingPattern(namingPatternEdit->text());
}

void ContestSettingsDialog::addRule() {
	const int row = rulesTable->rowCount();
	rulesTable->insertRow(row);
	auto *combo = new QComboBox(rulesTable);
	combo->addItem(tr("Command"), Violation::kindCommand());
	combo->addItem(tr("String"), Violation::kindString());
	rulesTable->setCellWidget(row, 0, combo);
	rulesTable->setItem(row, 1, new QTableWidgetItem(QString()));
	rulesTable->editItem(rulesTable->item(row, 1));
}

void ContestSettingsDialog::removeRule() {
	const int row = rulesTable->currentRow();

	if (row >= 0)
		rulesTable->removeRow(row);
}

void ContestSettingsDialog::restoreDefaults() {
	rulesTable->setRowCount(0);

	for (const ViolationRule &rule : Violation::defaultRules()) {
		const int row = rulesTable->rowCount();
		rulesTable->insertRow(row);
		auto *combo = new QComboBox(rulesTable);
		combo->addItem(tr("Command"), Violation::kindCommand());
		combo->addItem(tr("String"), Violation::kindString());
		combo->setCurrentIndex(rule.isCommand() ? 0 : 1);
		rulesTable->setCellWidget(row, 0, combo);
		rulesTable->setItem(row, 1, new QTableWidgetItem(rule.text));
	}
}

void ContestSettingsDialog::updateNamingHint() {
	const QString pattern = namingPatternEdit->text();
	const bool active = namingCheck->isChecked() && ! pattern.trimmed().isEmpty();

	if (! active) {
		namingHint->setText(tr("Parameters: <section> (region name), <number> (digit), <char> (letter)."));
		return;
	}

	namingHint->setText(tr("Contestant folders must be named like %1 (region sample: %2).")
	                        .arg(Naming::sample(pattern, sampleRegion), sampleRegion));
}
