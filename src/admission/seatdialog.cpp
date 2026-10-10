/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "seatdialog.h"
//
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

SeatDialog::SeatDialog(const AdmissionTable &table, const QList<AdmissionVenue> &venues,
                       const AdmissionAssign::SeatOptions &current, QWidget *parent)
    : QDialog(parent), table(table), venues(venues) {
	setWindowTitle(tr("Assign seats"));

	auto *layout = new QVBoxLayout(this);

	auto *form = new QFormLayout();
	layoutBox = new QComboBox(this);
	layoutBox->addItem(tr("Fill the first venues and rooms"), AdmissionAssign::FillFirst);
	layoutBox->addItem(tr("Spread evenly over the rooms"), AdmissionAssign::Balanced);
	layoutBox->setCurrentIndex(current.layout == AdmissionAssign::Balanced ? 1 : 0);
	orderBox = new QComboBox(this);
	orderBox->addItem(tr("List order"), QStringLiteral("row"));
	orderBox->addItem(tr("Random"), QStringLiteral("random"));
	orderBox->setCurrentIndex(qMax(0, orderBox->findData(current.order)));

	form->addRow(tr("Rooms:"), layoutBox);
	form->addRow(tr("Order:"), orderBox);
	layout->addLayout(form);

	preview = new QPlainTextEdit(this);
	preview->setObjectName(QStringLiteral("previewText"));
	preview->setReadOnly(true);
	preview->setLineWrapMode(QPlainTextEdit::NoWrap);
	preview->setMinimumHeight(200);
	preview->setTabChangesFocus(true);
	QFont mono = preview->font();
	mono.setFamily(QStringLiteral("monospace"));
	preview->setFont(mono);
	layout->addWidget(preview, 1);

	auto *row = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	auto *editButton = new QPushButton(tr("Edit venues / rooms…"), this);
	row->addButton(editButton, QDialogButtonBox::ResetRole);
	layout->addWidget(row);

	connect(layoutBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &SeatDialog::updatePreview);
	connect(orderBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &SeatDialog::updatePreview);
	connect(editButton, &QPushButton::clicked, this, &SeatDialog::editVenuesRequested);
	connect(row, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(row, &QDialogButtonBox::rejected, this, &QDialog::reject);

	updatePreview();
	resize(620, 520);
}

auto SeatDialog::options() const -> AdmissionAssign::SeatOptions {
	AdmissionAssign::SeatOptions value;
	value.layout = layoutBox->currentData().toInt();
	value.order = orderBox->currentData().toString();
	return value;
}

void SeatDialog::setVenues(const QList<AdmissionVenue> &value) {
	venues = value;
	updatePreview();
}

void SeatDialog::updatePreview() {
	QStringList venueValues;
	QStringList roomValues;
	QStringList seatValues;
	QString error;

	if (! AdmissionAssign::planSeats(table, venues, options(), venueValues, roomValues, seatValues, &error)) {
		preview->setPlainText(error);
		return;
	}

	const auto roomLine = [](const QString &name, int filled, int capacity) {
		return QStringLiteral("    %1    %2 / %3")
		    .arg(name, QString::number(filled), QString::number(capacity));
	};

	QStringList lines;

	if (seatValues.isEmpty()) {
		lines << tr("The list is empty.");
		preview->setPlainText(lines.join(QChar('\n')));
		return;
	}

	int width = 0;

	for (const QString &seat : seatValues)
		width = qMax(width, seat.size());

	lines << tr("Seat numbers: %1 digit(s)").arg(width);
	lines << QString();

	// 每个考点 / 考场分到多少人
	QHash<QString, QHash<QString, int>> perVenue;

	for (int row = 0; row < roomValues.size(); ++row)
		perVenue[venueValues.at(row)][roomValues.at(row)] += 1;

	for (const AdmissionVenue &venue : venues) {
		if (venue.rooms.isEmpty())
			continue;

		lines << venue.name;

		for (const AdmissionRoom &room : venue.rooms)
			lines << roomLine(room.name, perVenue.value(venue.name).value(room.name), qMax(1, room.capacity));
	}

	const int nameColumn = table.columnIndex(QStringLiteral("姓名"));
	lines << QString();

	// 全都列出来（文本框自己滚）：“谁坐在哪个考场哪个座位”能一路看到最后一个。
	for (int row = 0; row < seatValues.size(); ++row)
		lines << QStringLiteral("%1. %2 → %3 / %4")
		             .arg(row + 1)
		             .arg(table.cell(row, nameColumn), venueValues.at(row),
		                  roomValues.at(row) + QStringLiteral(" · ") + seatValues.at(row));

	preview->setPlainText(lines.join(QChar('\n')));
}
