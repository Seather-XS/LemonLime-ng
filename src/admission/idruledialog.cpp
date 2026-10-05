/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "idruledialog.h"
//
#include "admissionnaming.h"
//
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace {
	QStringList currentSources(const QList<QComboBox *> &boxes) {
		QStringList values;

		for (const QComboBox *box : boxes)
			values << box->currentData().toString();

		return values;
	}
} // namespace

IdRuleDialog::IdRuleDialog(const AdmissionTable &table, const QString &section,
                           const AdmissionAssign::IdOptions &current, QWidget *parent)
    : QDialog(parent), table(table), section(section), initialNumberSources(current.numberSources),
      initialCharSources(current.charSources) {
	setWindowTitle(tr("Ticket numbers"));

	auto *layout = new QVBoxLayout(this);

	auto *templateForm = new QFormLayout();
	templateEdit = new QLineEdit(current.templateText, this);
	templateEdit->setToolTip(tr("Placeholders: <section> <number> <char> <row> <seat> <room> <name> <id>, plus any "
	                            "column of the list (e.g. <学号>). Repeating a placeholder counts the digits: "
	                            "<number><number><number> is three digits."));
	templateForm->addRow(tr("Template:"), templateEdit);
	layout->addLayout(templateForm);

	groupsForm = new QFormLayout();
	layout->addLayout(groupsForm);

	auto *stateForm = new QFormLayout();
	overwriteBox = new QComboBox(this);
	overwriteBox->addItem(tr("Fill in the empty ones only"), false);
	overwriteBox->addItem(tr("Overwrite the existing ones"), true);
	overwriteBox->setCurrentIndex(current.overwrite ? 1 : 0);
	stateForm->addRow(tr("Existing ticket numbers:"), overwriteBox);
	layout->addLayout(stateForm);

	preview = new QLabel(this);
	preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont mono = preview->font();
	mono.setFamily(QStringLiteral("monospace"));
	preview->setFont(mono);
	preview->setMinimumHeight(90);
	preview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	layout->addWidget(preview, 1);

	buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);

	connect(templateEdit, &QLineEdit::textChanged, this, &IdRuleDialog::updateState);
	connect(overwriteBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &IdRuleDialog::updateState);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	updateState();
	resize(600, 420);
}

auto IdRuleDialog::options() const -> AdmissionAssign::IdOptions {
	AdmissionAssign::IdOptions value;
	value.templateText = templateEdit->text().trimmed();
	value.numberSources = currentSources(numberBoxes);
	value.charSources = currentSources(charBoxes);
	value.overwrite = overwriteBox->currentIndex() == 1;
	return value;
}

QComboBox *IdRuleDialog::makeSourceBox(const QString &key) {
	auto *box = new QComboBox(this);

	box->addItem(tr("Sequence in the region"), QStringLiteral("regionSeq"));
	box->addItem(tr("Sequence in the whole contest"), QStringLiteral("globalSeq"));
	box->addItem(tr("Row number of the list"), QStringLiteral("row"));
	box->addItem(tr("Seat number"), QStringLiteral("seat"));
	box->addItem(tr("Room number"), QStringLiteral("room"));
	box->addItem(tr("Digits at the end of the ticket number"), QStringLiteral("id"));
	box->addItem(tr("Digits at the end of the name"), QStringLiteral("name"));

	for (const QString &column : table.header)
		box->addItem(tr("Column: %1").arg(column), QStringLiteral("column:") + column);

	const int index = box->findData(key);
	box->setCurrentIndex(index < 0 ? 0 : index);

	// 放在 setCurrentIndex 之后：免得构造时就触发一次
	connect(box, qOverload<int>(&QComboBox::currentIndexChanged), this, &IdRuleDialog::updateState);
	return box;
}

void IdRuleDialog::syncGroups() {
	const QString pattern = templateEdit->text();
	const int numbers = AdmissionNaming::groupCount(pattern, QStringLiteral("number"));
	const int chars = AdmissionNaming::groupCount(pattern, QStringLiteral("char"));

	if (numbers == numberBoxes.size() && chars == charBoxes.size())
		return;

	const QStringList keepNumbers = currentSources(numberBoxes);
	const QStringList keepChars = currentSources(charBoxes);

	while (groupsForm->rowCount() > 0)
		groupsForm->removeRow(0);

	numberBoxes.clear();
	charBoxes.clear();

	for (int group = 0; group < numbers; ++group) {
		auto *box = makeSourceBox(keepNumbers.value(group, initialNumberSources.value(
		                                                 group, QStringLiteral("regionSeq"))));
		numberBoxes << box;
		groupsForm->addRow(tr("Digits from:"), box);
	}

	for (int group = 0; group < chars; ++group) {
		auto *box = makeSourceBox(
		    keepChars.value(group, initialCharSources.value(group, QStringLiteral("regionSeq"))));
		charBoxes << box;
		groupsForm->addRow(tr("Letters from:"), box);
	}
}

void IdRuleDialog::updateState() {
	syncGroups();
	updatePreview();
}

void IdRuleDialog::updatePreview() {
	QStringList ids;
	QString error;

	if (! AdmissionAssign::planIds(table, section, options(), ids, &error)) {
		preview->setText(error);
		return;
	}

	const int nameColumn = table.columnIndex(QStringLiteral("姓名"));
	const int rows = qMin(6, ids.size());
	QStringList lines;

	for (int row = 0; row < rows; ++row)
		lines << QStringLiteral("%1. %2 → %3")
		             .arg(row + 1)
		             .arg(table.cell(row, nameColumn), ids.at(row));

	if (ids.size() > rows)
		lines << QStringLiteral("…");

	preview->setText(lines.join(QChar('\n')));
}
