/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "venuedialog.h"
//
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
	constexpr int KindRole = Qt::UserRole;
	const QString KindVenue = QStringLiteral("venue");
	const QString KindRoom = QStringLiteral("room");

	/// 考点这一行：名字加粗，容量列留空（容量只有考场才有）。
	void makeVenueItem(QTreeWidgetItem *item, const QString &name) {
		item->setData(0, KindRole, KindVenue);
		item->setText(0, name);
		item->setText(1, QString());
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
		QFont font = item->font(0);
		font.setBold(true);
		item->setFont(0, font);
	}

	void makeRoomItem(QTreeWidgetItem *item, const QString &name, int capacity) {
		item->setData(0, KindRole, KindRoom);
		item->setText(0, name);
		item->setText(1, QString::number(qMax(1, capacity)));
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
	}
} // namespace

VenueDialog::VenueDialog(const QString &region, const QList<AdmissionVenue> &venues, QWidget *parent)
    : QDialog(parent), regionName(region) {
	setWindowTitle(tr("Venues — %1").arg(region.isEmpty() ? tr("(no region)") : region));

	auto *layout = new QVBoxLayout(this);

	tree = new QTreeWidget(this);
	tree->setColumnCount(2);
	tree->setHeaderLabels({tr("Venue / room"), tr("Capacity")});
	tree->setRootIsDecorated(true);
	tree->setUniformRowHeights(true);
	tree->header()->setStretchLastSection(false);
	tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	tree->setSelectionMode(QAbstractItemView::SingleSelection);
	layout->addWidget(tree, 1);

	auto *buttons = new QHBoxLayout();
	auto *addVenueButton = new QPushButton(tr("Add a venue"), this);
	auto *addRoomButton = new QPushButton(tr("Add a room"), this);
	auto *removeButton = new QPushButton(tr("Remove"), this);
	addVenueButton->setToolTip(tr("A region can have several venues."));
	addRoomButton->setToolTip(tr("The new room goes under the selected venue."));
	removeButton->setToolTip(tr("Removes the selected venue or room."));
	buttons->addWidget(addVenueButton);
	buttons->addWidget(addRoomButton);
	buttons->addWidget(removeButton);
	buttons->addStretch();
	layout->addLayout(buttons);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(box);

	connect(addVenueButton, &QPushButton::clicked, this, &VenueDialog::addVenue);
	connect(addRoomButton, &QPushButton::clicked, this, &VenueDialog::addRoom);
	connect(removeButton, &QPushButton::clicked, this, &VenueDialog::removeSelected);
	connect(tree, &QTreeWidget::itemChanged, this, &VenueDialog::itemChanged);
	connect(box, &QDialogButtonBox::accepted, this, &VenueDialog::acceptClicked);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

	for (const AdmissionVenue &venue : venues) {
		auto *venueItem = new QTreeWidgetItem(tree);
		makeVenueItem(venueItem, venue.name);

		for (const AdmissionRoom &room : venue.rooms)
			makeRoomItem(new QTreeWidgetItem(venueItem), room.name, room.capacity);
	}

	tree->expandAll();
	resize(560, 460);
}

auto VenueDialog::selectedVenue() const -> QTreeWidgetItem * {
	QTreeWidgetItem *item = tree->currentItem();

	if (! item)
		return nullptr;

	return item->data(0, KindRole).toString() == KindVenue ? item : item->parent();
}

void VenueDialog::addVenue() {
	auto *item = new QTreeWidgetItem(tree);
	makeVenueItem(item, tr("Venue %1").arg(tree->topLevelItemCount()));
	tree->setCurrentItem(item);
	tree->editItem(item, 0);
}

void VenueDialog::addRoom() {
	QTreeWidgetItem *venue = selectedVenue();

	if (! venue) {
		addVenue();
		venue = tree->currentItem();

		if (! venue || venue->data(0, KindRole).toString() != KindVenue)
			return;
	}

	auto *item = new QTreeWidgetItem(venue);
	makeRoomItem(item, tr("Room %1").arg(venue->childCount()), 30);
	venue->setExpanded(true);
	tree->setCurrentItem(item);
	tree->editItem(item, 0);
}

void VenueDialog::removeSelected() {
	QTreeWidgetItem *item = tree->currentItem();

	if (! item)
		return;

	if (item->data(0, KindRole).toString() == KindVenue)
		delete tree->takeTopLevelItem(tree->indexOfTopLevelItem(item));
	else
		delete item->parent()->takeChild(item->parent()->indexOfChild(item));
}

void VenueDialog::itemChanged(QTreeWidgetItem *item, int column) {
	if (updating || column != 1)
		return;

	updating = true;

	if (item->data(0, KindRole).toString() == KindRoom)
		item->setText(1, QString::number(qMax(1, item->text(1).toInt())));
	else
		item->setText(1, QString()); // 考点只有名字，容量是考场的事

	updating = false;
}

void VenueDialog::acceptClicked() {
	QStringList empty;

	for (int index = 0; index < tree->topLevelItemCount(); ++index) {
		if (tree->topLevelItem(index)->text(0).trimmed().isEmpty())
			empty << tr("venue %1").arg(index + 1);

		for (int child = 0; child < tree->topLevelItem(index)->childCount(); ++child)
			if (tree->topLevelItem(index)->child(child)->text(0).trimmed().isEmpty())
				empty << tr("room %1").arg(child + 1);
	}

	if (! empty.isEmpty()) {
		QMessageBox::warning(this, tr("Admission tickets"),
		                     tr("These entries have no name: %1").arg(empty.join(QStringLiteral(", "))));

		return;
	}

	accept();
}

auto VenueDialog::venues() const -> QList<AdmissionVenue> {
	QList<AdmissionVenue> result;

	for (int index = 0; index < tree->topLevelItemCount(); ++index) {
		const QTreeWidgetItem *venueItem = tree->topLevelItem(index);
		AdmissionVenue venue;
		venue.name = venueItem->text(0).trimmed();

		for (int child = 0; child < venueItem->childCount(); ++child) {
			const QTreeWidgetItem *roomItem = venueItem->child(child);
			AdmissionRoom room;
			room.name = roomItem->text(0).trimmed();
			room.capacity = qMax(1, roomItem->text(1).toInt());
			venue.rooms << room;
		}

		result << venue;
	}

	return result;
}
