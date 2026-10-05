/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "columndialog.h"
//
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ColumnDialog::ColumnDialog(const QString &name, const QStringList &columns,
                           const AdmissionNamingContext &context, const AdmissionColumnRule *existingRule,
                           QWidget *parent)
    : QDialog(parent), column(name), header(columns), sample(context), existingRule(existingRule) {
	buildUi();
	result = existingRule ? *existingRule : collect();
	updateState();
}

void ColumnDialog::buildUi() {
	auto *layout = new QVBoxLayout(this);

	auto *form = new QFormLayout();
	columnValue = new QLabel(this);
	QFont bold = columnValue->font();
	bold.setBold(true);
	columnValue->setFont(bold);
	typeLabel = new QLabel(this);
	typeBox = new QComboBox(this);
	typeBox->addItem(QString());
	typeBox->addItem(QString());
	form->addRow(tr("Column:"), columnValue);
	form->addRow(typeLabel, typeBox);
	layout->addLayout(form);

	// 模板
	templateBox = new QWidget(this);
	auto *templateForm = new QFormLayout(templateBox);
	templateForm->setContentsMargins(0, 0, 0, 0);
	templateLabel = new QLabel(templateBox);
	templateEdit = new QLineEdit(templateBox);
	templateForm->addRow(templateLabel, templateEdit);
	layout->addWidget(templateBox);

	// 随机
	randomBox = new QWidget(this);
	auto *randomForm = new QFormLayout(randomBox);
	randomForm->setContentsMargins(0, 0, 0, 0);
	lengthLabel = new QLabel(randomBox);
	lengthBox = new QSpinBox(randomBox);
	lengthBox->setRange(1, 64);
	randomForm->addRow(lengthLabel, lengthBox);

	charsetLabel = new QLabel(randomBox);
	auto *charsetWidget = new QWidget(randomBox);
	auto *charsetLayout = new QGridLayout(charsetWidget);
	charsetLayout->setContentsMargins(0, 0, 0, 0);
	charsetLayout->setHorizontalSpacing(14);
	upperBox = new QCheckBox(charsetWidget);
	lowerBox = new QCheckBox(charsetWidget);
	digitBox = new QCheckBox(charsetWidget);
	symbolBox = new QCheckBox(charsetWidget);
	ambiguousBox = new QCheckBox(charsetWidget);
	charsetLayout->addWidget(upperBox, 0, 0);
	charsetLayout->addWidget(lowerBox, 0, 1);
	charsetLayout->addWidget(digitBox, 1, 0);
	charsetLayout->addWidget(symbolBox, 1, 1);
	charsetLayout->addWidget(ambiguousBox, 2, 0, 1, 2);
	randomForm->addRow(charsetLabel, charsetWidget);

	recalculableBox = new QCheckBox(randomBox);
	randomForm->addRow(recalculableBox);

	sourceLabel = new QLabel(randomBox);
	seedBox = new QComboBox(randomBox);
	seedBox->addItem(QString(), QStringLiteral("row"));
	seedBox->addItem(QString(), QStringLiteral("id"));
	seedBox->addItem(QString(), QStringLiteral("name"));
	randomForm->addRow(sourceLabel, seedBox);
	layout->addWidget(randomBox);

	previewText = new QLabel(this);
	previewText->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont mono = previewText->font();
	mono.setFamily(QStringLiteral("monospace"));
	previewText->setFont(mono);
	layout->addWidget(previewText);

	buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);

	connect(typeBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &ColumnDialog::updateState);
	connect(templateEdit, &QLineEdit::textChanged, this, &ColumnDialog::updateState);
	connect(lengthBox, qOverload<int>(&QSpinBox::valueChanged), this, &ColumnDialog::updateState);
	connect(seedBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &ColumnDialog::updateState);

	for (QCheckBox *box : {upperBox, lowerBox, digitBox, symbolBox, ambiguousBox, recalculableBox})
		connect(box, &QCheckBox::checkStateChanged, this, &ColumnDialog::updateState);

	connect(buttons, &QDialogButtonBox::accepted, this, &ColumnDialog::acceptClicked);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	load();
	retranslate();
	resize(560, 360);
}

void ColumnDialog::retranslate() {
	setWindowTitle(tr("Column settings — %1").arg(column));
	columnValue->setText(column);
	typeLabel->setText(tr("Filled by:"));
	typeBox->setItemText(0, tr("Template"));
	typeBox->setItemText(1, tr("Random"));
	templateLabel->setText(tr("Template:"));
	templateEdit->setToolTip(tr("Placeholders: <number> <char> <row> <col> <name> <id> <seat> <section>, and other "
	                           "columns of the list (e.g. <学号>)."));
	lengthLabel->setText(tr("Length:"));
	charsetLabel->setText(tr("Characters:"));
	upperBox->setText(tr("Upper case (A-Z)"));
	lowerBox->setText(tr("Lower case (a-z)"));
	digitBox->setText(tr("Digits (0-9)"));
	symbolBox->setText(tr("Symbols (!@#$…)"));
	ambiguousBox->setText(tr("Exclude look-alike characters"));
	recalculableBox->setText(tr("Keep the value stable"));
	sourceLabel->setText(tr("Source:"));
	seedBox->setItemText(0, tr("Row number"));
	seedBox->setItemText(1, tr("Ticket number"));
	seedBox->setItemText(2, tr("Name"));
	buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
	buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
}

void ColumnDialog::load() {
	if (! existingRule) {
		typeBox->setCurrentIndex(0);
		templateEdit->setText(QStringLiteral("<number><number><number>"));
		lengthBox->setValue(8);
		upperBox->setChecked(true);
		lowerBox->setChecked(true);
		digitBox->setChecked(true);
		symbolBox->setChecked(false);
		ambiguousBox->setChecked(true);
		recalculableBox->setChecked(false);
		seedBox->setCurrentIndex(0);
		return;
	}

	typeBox->setCurrentIndex(existingRule->type == QStringLiteral("random") ? 1 : 0);
	templateEdit->setText(existingRule->templateText);
	lengthBox->setValue(existingRule->length);
	upperBox->setChecked(existingRule->upper);
	lowerBox->setChecked(existingRule->lower);
	digitBox->setChecked(existingRule->digit);
	symbolBox->setChecked(existingRule->symbol);
	ambiguousBox->setChecked(existingRule->excludeAmbiguous);
	recalculableBox->setChecked(existingRule->recalculable);

	for (int index = 0; index < seedBox->count(); ++index)
		if (seedBox->itemData(index).toString() == existingRule->seedSource) {
			seedBox->setCurrentIndex(index);
			break;
		}
}

auto ColumnDialog::collect() const -> AdmissionColumnRule {
	AdmissionColumnRule value;
	value.name = column;
	value.type = typeBox->currentIndex() == 1 ? QStringLiteral("random") : QStringLiteral("template");
	value.templateText = templateEdit->text().trimmed();
	// 模板里的数字 / 字母分组默认按赛区序号，改来源的界面留到以后。
	value.numberSources = {QStringLiteral("regionSeq")};
	value.charSources = {QStringLiteral("regionSeq")};
	value.length = lengthBox->value();
	value.upper = upperBox->isChecked();
	value.lower = lowerBox->isChecked();
	value.digit = digitBox->isChecked();
	value.symbol = symbolBox->isChecked();
	value.excludeAmbiguous = ambiguousBox->isChecked();
	value.recalculable = recalculableBox->isChecked();
	value.seedSource = seedBox->currentData().toString();
	return value;
}

void ColumnDialog::updateState() {
	const bool random = typeBox->currentIndex() == 1;
	templateBox->setVisible(! random);
	randomBox->setVisible(random);
	sourceLabel->setEnabled(random && recalculableBox->isChecked());
	seedBox->setEnabled(random && recalculableBox->isChecked());

	QString error;
	const QString value = AdmissionColumns::preview(collect(), sample, &error);

	if (! error.isEmpty()) {
		previewText->setStyleSheet(QStringLiteral("color: #c0392b;"));
		previewText->setText(tr("Preview: %1").arg(error));
		return;
	}

	previewText->setStyleSheet(QString());
	previewText->setText(tr("Preview: %1").arg(value.isEmpty() ? QStringLiteral("—") : value));
}

void ColumnDialog::acceptClicked() {
	const AdmissionColumnRule value = collect();

	if (value.type == QStringLiteral("template")) {
		QString error;

		if (! AdmissionNaming::validate(value.templateText, header, &error)) {
			QMessageBox::warning(this, tr("Column settings"), error);
			return;
		}
	} else if (! value.upper && ! value.lower && ! value.digit && ! value.symbol) {
		QMessageBox::warning(this, tr("Column settings"), tr("Select at least one character set."));
		return;
	}

	result = value;
	accept();
}
