/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "contestsettingsdialog.h"

#include "base/compiler.h"
#include "base/settings.h"
#include "core/contest.h"
#include "core/contestant.h"
#include "core/naming.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

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

	// —— 违规检测：规则跟着编译器（语言）走，这里只勾选用哪些语言 ——
	auto *violationGroup = new QGroupBox(tr("Violation checking"), this);
	auto *violationLayout = new QVBoxLayout(violationGroup);
	violationCheck = new QCheckBox(tr("Enable violation checking"), violationGroup);
	violationLayout->addWidget(violationCheck);
	violationHint = new QLabel(tr("Rules are configured per compiler in Options → Compiler → Advanced."),
	                           violationGroup);
	violationHint->setWordWrap(true);
	violationLayout->addWidget(violationHint);
	compilerList = new QListWidget(violationGroup);
	violationLayout->addWidget(compilerList, 1);
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
	connect(violationCheck, &QCheckBox::toggled, this, &ContestSettingsDialog::updateViolationState);
	connect(namingPatternEdit, &QLineEdit::textChanged, this, &ContestSettingsDialog::updateNamingHint);
	connect(namingCheck, &QCheckBox::toggled, this, &ContestSettingsDialog::updateNamingHint);
}

void ContestSettingsDialog::setSettings(Settings *_settings) { settings = _settings; }

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

	compilerList->clear();

	if (settings) {
		for (auto *compiler : settings->getCompilerList()) {
			const QString name = compiler->getCompilerName();
			const int rules = compiler->getViolationRules().size();
			auto *item = new QListWidgetItem(
			    rules > 0 ? tr("%1 (%2 rules)").arg(name).arg(rules) : tr("%1 (no rules)").arg(name),
			    compilerList);
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
			item->setCheckState(contest->isViolationCompilerSelected(name) ? Qt::Checked : Qt::Unchecked);
			item->setData(Qt::UserRole, name);
		}
	}

	updateViolationState();
	updateNamingHint();
}

void ContestSettingsDialog::applyTo(Contest *contest) const {
	if (! contest)
		return;

	contest->setRegionEnabled(regionCheck->isChecked());
	contest->setViolationCheck(violationCheck->isChecked());

	QStringList compilers;

	for (int row = 0; row < compilerList->count(); row++) {
		const QListWidgetItem *item = compilerList->item(row);

		if (item->checkState() == Qt::Checked)
			compilers.append(item->data(Qt::UserRole).toString());
	}

	contest->setViolationCompilers(compilers);
	contest->setNamingCheck(namingCheck->isChecked());
	contest->setNamingPattern(namingPatternEdit->text());
}

void ContestSettingsDialog::updateViolationState() {
	const bool enabled = violationCheck->isChecked();
	compilerList->setEnabled(enabled);
	violationHint->setEnabled(enabled);
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
