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
#include <QLineEdit>
#include <QPlainTextEdit>
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
    : QDialog(parent), table(table), section(section), initialCharSources(current.charSources) {
	setWindowTitle(tr("Ticket numbers"));

	auto *layout = new QVBoxLayout(this);

	auto *templateForm = new QFormLayout();
	templateEdit = new QLineEdit(current.templateText, this);
	templateEdit->setToolTip(tr("Placeholders: <section> <number> <char> <row> <seat> <room> <name> <id>, plus any "
	                            "column of the list (e.g. <学号>). The digits always come from the row number of "
	                            "the list; repeating the placeholder sets the width: <number><number><number> is "
	                            "001, 002, …"));
	templateForm->addRow(tr("Template:"), templateEdit);
	layout->addLayout(templateForm);

	groupsForm = new QFormLayout();
	layout->addLayout(groupsForm);

	// 预览放在只读文本框里：人数一多标签就把对话框撑爆了，这里让它自己滚。
	preview = new QPlainTextEdit(this);
	preview->setObjectName(QStringLiteral("previewText"));
	preview->setReadOnly(true);
	preview->setLineWrapMode(QPlainTextEdit::NoWrap);
	preview->setMinimumHeight(160);
	preview->setTabChangesFocus(true);
	QFont mono = preview->font();
	mono.setFamily(QStringLiteral("monospace"));
	preview->setFont(mono);
	layout->addWidget(preview, 1);

	buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);

	connect(templateEdit, &QLineEdit::textChanged, this, &IdRuleDialog::updateState);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	updateState();
	resize(600, 460);
}

auto IdRuleDialog::options() const -> AdmissionAssign::IdOptions {
	AdmissionAssign::IdOptions value;
	value.templateText = templateEdit->text().trimmed();
	value.charSources = currentSources(charBoxes);
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
	const int chars = AdmissionNaming::groupCount(pattern, QStringLiteral("char"));

	// 数字固定取行号，没有下拉；只有模板里写了 <char> 才有「字母取自」。
	if (chars == charBoxes.size())
		return;

	const QStringList keepChars = currentSources(charBoxes);

	while (groupsForm->rowCount() > 0)
		groupsForm->removeRow(0);

	charBoxes.clear();

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
		preview->setPlainText(error);
		return;
	}

	const int nameColumn = table.columnIndex(QStringLiteral("姓名"));
	QStringList lines;

	// 全都列出来：预览是能滚的文本框，不用再截断，用户想看第 500 个号码也行。
	for (int row = 0; row < ids.size(); ++row)
		lines << QStringLiteral("%1. %2 → %3")
		             .arg(row + 1)
		             .arg(table.cell(row, nameColumn), ids.at(row));

	if (lines.isEmpty())
		lines << tr("The list is empty.");

	preview->setPlainText(lines.join(QChar('\n')));
}
