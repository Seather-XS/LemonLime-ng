/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "csveditordialog.h"
//
#include "admissionassign.h"
#include "admissioncolumns.h"
#include "admissionnaming.h"
#include "admissionproject.h"
#include "columndialog.h"
#include "idruledialog.h"
#include "seatdialog.h"
#include "venuedialog.h"
//
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <utility>

namespace {
	enum FillMode { FillFixed = 0, FillSequence, FillRowNumber, FillClear };

	/// 「序列」的起点：纯数字列就从 1 开始，否则给个 A1、A2 那样的。
	QString sequenceValue(const QString &seed, int index) {
		const QString base = seed.trimmed().isEmpty() ? QStringLiteral("1") : seed.trimmed();
		bool numeric = false;
		const int value = base.toInt(&numeric);

		if (numeric)
			return QString::number(value + index);

		return base + QString::number(index + 1);
	}

	/// 单元格文字（没有 item 就是空的）。
	QString cellTextOf(const QTableWidget *grid, int row, int column) {
		const QTableWidgetItem *item = grid->item(row, column);
		return item ? item->text().trimmed() : QString();
	}

	/// 照片路径：绝对路径直接用，否则先按赛区目录找，再按比赛日目录找。
	QString resolvePhoto(const QString &region, const QString &value) {
		const QString trimmed = value.trimmed();

		if (trimmed.isEmpty())
			return QString();

		if (QFileInfo(trimmed).isAbsolute())
			return QFileInfo::exists(trimmed) ? trimmed : QString();

		const QString inRegion = AdmissionProject::regionFolder(region) + trimmed;

		if (QFileInfo::exists(inRegion))
			return inRegion;

		return QFileInfo::exists(trimmed) ? trimmed : QString();
	}

	/// 撤销栈里的一步：两个快照之间的跳（整表一步）。
	class FunctionCommand : public QUndoCommand {
	  public:
		FunctionCommand(QString text, std::function<void()> undoFunction, std::function<void()> redoFunction)
		    : QUndoCommand(std::move(text)), undoFunction(std::move(undoFunction)),
		      redoFunction(std::move(redoFunction)) {}

		void undo() override { undoFunction(); }
		void redo() override { redoFunction(); }

	  private:
		std::function<void()> undoFunction;
		std::function<void()> redoFunction;
	};
} // namespace

FillTable::FillTable(QWidget *parent) : QTableWidget(parent) { setMouseTracking(true); }

auto FillTable::selectionRect() const -> QRect {
	if (selectedRanges().isEmpty())
		return QRect();

	const QTableWidgetSelectionRange range = selectedRanges().constFirst();
	return QRect(QPoint(range.leftColumn(), range.topRow()),
	             QPoint(range.rightColumn(), range.bottomRow()));
}

auto FillTable::handleRect() const -> QRect {
	const QRect range = selectionRect();

	if (range.isEmpty() || ! model())
		return QRect();

	const QRect last = visualRect(model()->index(range.bottom(), range.right()));

	if (! last.isValid())
		return QRect();

	return QRect(last.right() - 3, last.bottom() - 3, 7, 7);
}

void FillTable::paintEvent(QPaintEvent *event) {
	QTableWidget::paintEvent(event);
	const QRect range = selectionRect();
	const QRect handle = handleRect();

	if (range.isEmpty() || ! handle.isValid() || ! model())
		return;

	QPainter painter(viewport());
	const QRect first = visualRect(model()->index(range.top(), range.left()));
	const QRect last = visualRect(model()->index(range.bottom(), range.right()));
	const QRect border(first.topLeft(), last.bottomRight());
	painter.setPen(QPen(QColor(0x2d, 0x8c, 0xf0), 2));
	painter.drawRect(border.adjusted(0, 0, 1, 1));

	if (dragging && target.isValid() && target != source) {
		painter.setPen(QPen(QColor(0x2d, 0x8c, 0xf0), 1, Qt::DashLine));
		const QRect targetFirst = visualRect(model()->index(target.top(), target.left()));
		const QRect targetLast = visualRect(model()->index(target.bottom(), target.right()));
		painter.drawRect(QRect(targetFirst.topLeft(), targetLast.bottomRight()).adjusted(0, 0, 1, 1));
	}

	painter.setPen(QPen(QColor(0x2d, 0x8c, 0xf0)));
	painter.setBrush(dragging ? QColor(0x2d, 0x8c, 0xf0) : QColor(Qt::white));
	painter.drawRect(handle);
}

void FillTable::mousePressEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton && handleRect().adjusted(-2, -2, 2, 2).contains(event->pos())) {
		dragging = true;
		source = selectionRect();
		target = source;
		update();
		return;
	}

	QTableWidget::mousePressEvent(event);
}

void FillTable::mouseMoveEvent(QMouseEvent *event) {
	if (! dragging) {
		setCursor(handleRect().adjusted(-2, -2, 2, 2).contains(event->pos()) ? Qt::CrossCursor
		                                                                     : Qt::ArrowCursor);
		QTableWidget::mouseMoveEvent(event);
		return;
	}

	const QModelIndex index = indexAt(event->pos());

	// 只支持往下 / 往右拖
	if (index.isValid()) {
		QRect wanted = source;

		if (index.row() > source.bottom())
			wanted.setBottom(index.row());

		if (index.column() > source.right())
			wanted.setRight(index.column());

		if (wanted != target) {
			target = wanted;
			update();
		}
	}
}

void FillTable::mouseReleaseEvent(QMouseEvent *event) {
	if (dragging) {
		dragging = false;
		const QRect from = source;
		const QRect to = target;
		target = QRect();
		update();

		if (to != from)
			emit fillRequested(from, to);

		return;
	}

	QTableWidget::mouseReleaseEvent(event);
}

CsvEditorDialog::CsvEditorDialog(const QString &name, AdmissionTable &value, AdmissionProject *owner,
                                 QWidget *parent)
    : QDialog(parent), region(name), table(value), project(owner) {
	buildUi();
	load();
	lastState = serialize();
}

void CsvEditorDialog::buildUi() {
	auto *layout = new QVBoxLayout(this);

	grid = new FillTable(this);
	grid->setColumnCount(1);
	grid->setSelectionBehavior(QAbstractItemView::SelectItems);
	grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
	grid->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked |
	                      QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
	grid->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
	grid->horizontalHeader()->setStretchLastSection(true);
	grid->setContextMenuPolicy(Qt::CustomContextMenu);
	grid->setMinimumSize(720, 300);
	grid->setObjectName(QStringLiteral("listGrid"));

	auto *splitter = new QSplitter(Qt::Vertical, this);
	splitter->addWidget(grid);

	if (project) {
		// 下半部分：本赛区的注意事项（纯文本，只认 [文字](链接) 这种超链接）
		auto *bottom = new QWidget(splitter);
		auto *bottomLayout = new QVBoxLayout(bottom);
		bottomLayout->setContentsMargins(6, 6, 6, 0);
		bottomLayout->addWidget(new QLabel(tr("Notes"), bottom));
		notesEdit = new QPlainTextEdit(bottom);
		notesEdit->setObjectName(QStringLiteral("notesEdit"));
		notesEdit->setMinimumHeight(80);
		bottomLayout->addWidget(notesEdit, 1);
		splitter->addWidget(bottom);
		splitter->setStretchFactor(0, 3);
		splitter->setStretchFactor(1, 1);

		if (const AdmissionRegion *item = project->find(region))
			notesEdit->setPlainText(item->notes);
	}

	layout->addWidget(splitter, 1);

	undoStack = new QUndoStack(this);
	undoStack->setUndoLimit(100);
	// 撤销 / 重做只留快捷键（右键菜单里的那些操作都自带一步撤销）。
	auto *undoAction = undoStack->createUndoAction(this, tr("Undo"));
	undoAction->setShortcut(QKeySequence::Undo);
	auto *redoAction = undoStack->createRedoAction(this, tr("Redo"));
	redoAction->setShortcut(QKeySequence::Redo);
	addAction(undoAction);
	addAction(redoAction);

	auto *bar = new QHBoxLayout();
	auto *validateButton = new QPushButton(tr("Check"), this);
	auto *seatsButton = new QPushButton(tr("Assign seats"), this);
	auto *venuesButton = new QPushButton(tr("Venues / rooms"), this);
	auto *idsButton = new QPushButton(tr("Generate ticket numbers"), this);
	venuesButton->setObjectName(QStringLiteral("venuesButton"));
	bar->addStretch();
	bar->addWidget(venuesButton);
	bar->addWidget(seatsButton);
	bar->addWidget(idsButton);
	bar->addWidget(validateButton);
	layout->addLayout(bar);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);

	connect(validateButton, &QPushButton::clicked, this, &CsvEditorDialog::validateClicked);
	connect(seatsButton, &QPushButton::clicked, this, &CsvEditorDialog::seatsClicked);
	connect(venuesButton, &QPushButton::clicked, this, &CsvEditorDialog::venuesClicked);
	connect(idsButton, &QPushButton::clicked, this, &CsvEditorDialog::idsClicked);
	connect(grid->horizontalHeader(), &QWidget::customContextMenuRequested, this,
	        &CsvEditorDialog::headerMenu);
	connect(grid, &QWidget::customContextMenuRequested, this, &CsvEditorDialog::gridMenu);
	connect(grid, &FillTable::fillRequested, this, &CsvEditorDialog::fillRequested);
	connect(grid, &QTableWidget::itemChanged, this, &CsvEditorDialog::gridEdited);
	connect(buttons, &QDialogButtonBox::accepted, this, &CsvEditorDialog::saveClicked);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

// 双击最后一行的空行 = 在末尾再添一行（名单长了不用来回滚）
	connect(grid, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
		if (row != grid->rowCount() - 1 || grid->rowCount() == 0)
			return;

		for (int column = 0; column < grid->columnCount(); ++column)
			if (grid->item(row, column) && ! grid->item(row, column)->text().trimmed().isEmpty())
				return;

		addRowAtEnd();
	});

	resize(900, 780);
	retranslate();
}

void CsvEditorDialog::retranslate() {
	setWindowTitle(tr("Contestants — %1").arg(region.isEmpty() ? tr("(no region)") : region));
}

void CsvEditorDialog::load() {
	const bool previous = muted;
	muted = true;
	grid->clear();
	grid->setColumnCount(table.header.size());
	grid->setHorizontalHeaderLabels(table.header);
	grid->setRowCount(0);

	for (int row = 0; row < table.rows.size(); ++row) {
		grid->insertRow(row);

		for (int column = 0; column < table.header.size(); ++column)
			grid->setItem(row, column, new QTableWidgetItem(table.cell(row, column)));
	}

	// 照片列：行高放大一点，顺便把缩略图画出来
	const int photo = photoColumn();

	if (photo >= 0) {
		grid->setIconSize(QSize(40, 40));
		grid->verticalHeader()->setDefaultSectionSize(46);

		for (int index = 0; index < table.rows.size(); ++index)
			setPhotoCell(index, photo, table.cell(index, photo));
	}

	muted = previous;
}

auto CsvEditorDialog::rowsAsText() const -> QStringList {
	QStringList lines;

	for (int row = 0; row < grid->rowCount(); ++row) {
		QStringList cells;
		bool empty = true;

		for (int column = 0; column < grid->columnCount(); ++column) {
			const QString value = grid->item(row, column) ? grid->item(row, column)->text().trimmed()
			                                             : QString();
			cells << value;

			if (! value.isEmpty())
				empty = false;
		}

		if (! empty)
			lines << cells.join(QChar('\t'));
	}

	return lines;
}

void CsvEditorDialog::store() {
	table.header.clear();

	for (int column = 0; column < grid->columnCount(); ++column) {
		const QTableWidgetItem *header = grid->horizontalHeaderItem(column);
		table.header << (header ? header->text().trimmed() : QString());
	}

	table.rows.clear();

	for (const QString &line : rowsAsText())
		table.rows << line.split(QChar('\t'));

	table.normalize();
}

void CsvEditorDialog::saveClicked() {
	store();

	if (project) {
		AdmissionRegion *item = project->find(region);

		if (item && notesEdit)
			item->notes = notesEdit->toPlainText();

		QString error;

		if (! project->save(&error)) {
			QMessageBox::warning(this, tr("Admission tickets"), error);
			return;
		}
	}

	accept();
}

auto CsvEditorDialog::serialize() const -> QString {
	QStringList header;

	for (int column = 0; column < grid->columnCount(); ++column)
		header << (grid->horizontalHeaderItem(column) ? grid->horizontalHeaderItem(column)->text().trimmed()
		                                            : QString());

	QStringList lines;
	lines << header.join(QChar('\t'));
	lines << rowsAsText();
	return lines.join(QChar('\n'));
}

void CsvEditorDialog::applyState(const QString &state) {
	const QStringList lines = state.split(QChar('\n'));
	// 这一段会整表重填，itemChanged 会被触发；期间绝不能记撤销。
	const bool previous = applyingState;
	applyingState = true;
	table.header = lines.value(0).split(QChar('\t'));
	table.rows.clear();

	for (int index = 1; index < lines.size(); ++index)
		if (! lines.at(index).isEmpty())
			table.rows << lines.at(index).split(QChar('\t'));

	table.normalize();
	const bool previousMuted = muted;
	muted = true;
	load();
	muted = previousMuted;
	applyingState = previous;
	lastState = serialize();
}

void CsvEditorDialog::pushState(const QString &text) {
	// 撤销 / 重做正在把快照写回界面时不能再 push（后面还有 command_list.at() 要用，会把栈搞乱）。
	if (applyingState || ! undoStack)
		return;

	muted = false;
	const QString now = serialize();

	if (now == lastState)
		return;

	const QString before = lastState;
	lastState = now;
	suppressCommandRedo = true;
	undoStack->push(new FunctionCommand(text, [this, before] { applyState(before); },
	                                    [this, now] {
		                                    if (! suppressCommandRedo)
			                                    applyState(now);
	                                    }));
	suppressCommandRedo = false;
}

void CsvEditorDialog::gridEdited() {
	// 程序自己刷界面（load / 应用快照）时不要记撤销。
	if (muted)
		return;

	pushState(tr("Edit a cell"));
}

void CsvEditorDialog::fillRequested(const QRect &source, const QRect &target) {
	if (source.isEmpty() || target.isEmpty() || target == source)
		return;

	const int width = source.width();
	const int height = source.height();
	QList<QStringList> block;

	for (int row = source.top(); row <= source.bottom(); ++row) {
		QStringList cells;

		for (int column = source.left(); column <= source.right(); ++column)
			cells << cellTextOf(grid, row, column);

		block << cells;
	}

	// 单列（往下拖）/ 单行（往右拖）且都是数字时递增，其余按块循环复制。
	const bool singleColumn = width == 1 && target.right() <= source.right();
	const bool singleRow = height == 1 && target.bottom() <= source.bottom();
	bool increment = singleColumn || singleRow;
	QList<int> numbers;

	if (increment)
		for (int row = source.top(); row <= source.bottom() && increment; ++row)
			for (int column = source.left(); column <= source.right() && increment; ++column) {
				bool ok = false;
				const int value = cellTextOf(grid, row, column).toInt(&ok);

				if (! ok)
					increment = false;
				else
					numbers << value;
			}

	const int step = numbers.size() >= 2 ? numbers.at(1) - numbers.at(0) : 1;

	muted = true;

	for (int row = target.top(); row <= target.bottom(); ++row) {
		for (int column = target.left(); column <= target.right(); ++column) {
			if (source.contains(QPoint(column, row)))
				continue;

			QTableWidgetItem *item = grid->item(row, column);

			if (! item) {
				item = new QTableWidgetItem();
				grid->setItem(row, column, item);
			}

			QString value;

			if (increment && singleColumn && row > source.bottom())
				value = QString::number(numbers.last() + step * (row - source.bottom()));
			else if (increment && singleRow && column > source.right())
				value = QString::number(numbers.last() + step * (column - source.right()));
			else
				value = block.at((row - source.top()) % height).at((column - source.left()) % width);

			item->setText(value);
		}
	}

	pushState(tr("Fill cells"));
}

void CsvEditorDialog::showColumnStatistics(int column) {
	if (column < 0 || column >= grid->columnCount())
		return;

	const int rows = rowsAsText().size();
	QHash<QString, int> counts;
	int filled = 0;
	int minLength = -1;
	int maxLength = 0;

	for (int row = 0; row < grid->rowCount(); ++row) {
		const QString value = cellTextOf(grid, row, column);

		if (value.isEmpty())
			continue;

		++filled;
		++counts[value];
		const int length = value.length();
		minLength = minLength < 0 ? length : qMin(minLength, length);
		maxLength = qMax(maxLength, length);
	}

	QStringList duplicates;

	for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
		if (it.value() > 1)
			duplicates << QStringLiteral("%1 ×%2").arg(it.key()).arg(it.value());

	std::sort(duplicates.begin(), duplicates.end());

	QStringList lines;
	lines << tr("Column: %1").arg(grid->horizontalHeaderItem(column)->text());
	lines << tr("Filled: %1 / %2").arg(filled).arg(rows);
	lines << tr("Distinct: %1").arg(counts.size());
	lines << tr("Length: %1 – %2").arg(minLength < 0 ? 0 : minLength).arg(maxLength);

	if (! duplicates.isEmpty()) {
		lines << tr("Duplicated:");

		for (int index = 0; index < duplicates.size() && index < 10; ++index)
			lines << QStringLiteral("    ") + duplicates.at(index);

		if (duplicates.size() > 10)
			lines << QStringLiteral("    …");
	}

	QMessageBox::information(this, tr("Column statistics"), lines.join(QChar('\n')));
}

void CsvEditorDialog::fillColumn(int column, int mode) {
	if (column < 0 || column >= grid->columnCount())
		return;

	QString seed;

	if (mode == FillFixed || mode == FillSequence) {
		bool ok = false;
		seed = QInputDialog::getText(this, windowTitle(),
		                             mode == FillFixed ? tr("Value for this column:")
		                                               : tr("Start of the sequence (empty = 1):"),
		                             QLineEdit::Normal, QString(), &ok);

		if (! ok)
			return;
	}

	// 只填到最后一个有内容的行（后面本来就没数据的行不用管）
	int last = grid->rowCount() - 1;

	while (last >= 0) {
		bool empty = true;

		for (int c = 0; c < grid->columnCount(); ++c)
			if (grid->item(last, c) && ! grid->item(last, c)->text().trimmed().isEmpty())
				empty = false;

		if (! empty)
			break;

		--last;
	}

	muted = true;
	int index = 0;

	for (int row = 0; row <= last; ++row) {
		QTableWidgetItem *item = grid->item(row, column);

		if (! item) {
			item = new QTableWidgetItem();
			grid->setItem(row, column, item);
		}

		switch (mode) {
			case FillFixed: item->setText(seed); break;
			case FillSequence: item->setText(sequenceValue(seed, index)); ++index; break;
			case FillRowNumber: item->setText(QString::number(row + 1)); break;
			case FillClear: item->setText(QString()); break;
			default: break;
		}
	}

	grid->resizeColumnToContents(column);
	pushState(tr("Fill the column"));
}

void CsvEditorDialog::fillSelection(int mode) {
	const QList<QTableWidgetSelectionRange> ranges = grid->selectedRanges();

	if (ranges.isEmpty())
		return;

	QString seed;

	if (mode == FillFixed || mode == FillSequence) {
		bool ok = false;
		seed = QInputDialog::getText(this, windowTitle(),
		                             mode == FillFixed ? tr("Value for the selection:")
		                                               : tr("Start of the sequence (empty = 1):"),
		                             QLineEdit::Normal, QString(), &ok);

		if (! ok)
			return;
	}

	muted = true;
	int index = 0;

	for (const QTableWidgetSelectionRange &range : ranges) {
		for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
			for (int column = range.leftColumn(); column <= range.rightColumn(); ++column) {
				QTableWidgetItem *item = grid->item(row, column);

				if (! item) {
					item = new QTableWidgetItem();
					grid->setItem(row, column, item);
				}

				switch (mode) {
					case FillFixed: item->setText(seed); break;
					case FillSequence: item->setText(sequenceValue(seed, index)); ++index; break;
					case FillClear: item->setText(QString()); break;
					default: break;
				}
			}
		}
	}

	pushState(tr("Fill the selection"));
}

void CsvEditorDialog::addColumn() {
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Add column"), tr("Column name:"),
	                                          QLineEdit::Normal, QString(), &ok);

	if (! ok || name.trimmed().isEmpty())
		return;

	const QString trimmed = name.trimmed();

	if (table.header.contains(trimmed) ||
	    grid->findItems(trimmed, Qt::MatchExactly).size() > 0) {
		QMessageBox::information(this, tr("Admission tickets"),
		                         tr("The column %1 already exists.").arg(trimmed));
		return;
	}

	store();
	table.header << trimmed;

	for (QStringList &row : table.rows)
		row << QString();

	load();
	grid->setCurrentCell(0, grid->columnCount() - 1);
	pushState(tr("Add a column"));
}

void CsvEditorDialog::removeColumns(const QList<int> &columns) {
	QList<int> targets;
	QStringList skipped;

	for (int column : columns) {
		if (column < 0 || column >= grid->columnCount() || targets.contains(column))
			continue;

		const QTableWidgetItem *header = grid->horizontalHeaderItem(column);

		if (header && AdmissionTable::builtinColumns().contains(header->text()))
			skipped << header->text();
		else
			targets << column;
	}

	if (targets.isEmpty()) {
		if (! skipped.isEmpty())
			QMessageBox::information(this, tr("Admission tickets"),
			                         tr("The built-in columns cannot be removed."));
		return;
	}

	// 直接删表格里的列：不动 table 再 load()，否则列号会因为 normalize() 重排而错位。
	std::sort(targets.begin(), targets.end(), std::greater<int>());
	QStringList removed;
	muted = true;

	for (int column : std::as_const(targets)) {
		removed << grid->horizontalHeaderItem(column)->text();
		grid->removeColumn(column);
	}

	dropColumnRules(removed);
	store();
	pushState(tr("Delete columns"));
}

void CsvEditorDialog::removeSelectedColumns() {
	const QList<QTableWidgetSelectionRange> ranges = grid->selectedRanges();
	QList<int> columns;

	for (const QTableWidgetSelectionRange &range : ranges)
		for (int column = range.leftColumn(); column <= range.rightColumn(); ++column)
			columns << column;

	removeColumns(columns);
}

void CsvEditorDialog::removeSelectedRows() {
	const QList<QTableWidgetSelectionRange> ranges = grid->selectedRanges();
	QList<int> rows;

	for (const QTableWidgetSelectionRange &range : ranges)
		for (int row = range.topRow(); row <= range.bottomRow(); ++row)
			if (! rows.contains(row))
				rows << row;

	if (rows.isEmpty())
		return;

	// 直接删表格里的行：table.rows 会跳过空行，按下标去删会删错行。
	std::sort(rows.begin(), rows.end(), std::greater<int>());
	muted = true;

	for (int row : std::as_const(rows))
		if (row < grid->rowCount())
			grid->removeRow(row);

	store();
	pushState(tr("Delete rows"));
}

void CsvEditorDialog::addRowAtEnd() {
	const int row = grid->rowCount();
	muted = true;
	grid->insertRow(row);

	for (int column = 0; column < grid->columnCount(); ++column)
		grid->setItem(row, column, new QTableWidgetItem());

	store();
	pushState(tr("Add a row"));
	grid->setCurrentCell(row, 0);
}

void CsvEditorDialog::dropColumnRules(const QStringList &names) {
	if (names.isEmpty())
		return;

	QList<AdmissionColumnRule> rules;
	QString error;

	if (! AdmissionColumns::load(region, rules, &error) && ! error.isEmpty())
		return;

	QList<AdmissionColumnRule> kept;

	for (const AdmissionColumnRule &rule : std::as_const(rules))
		if (! names.contains(rule.name))
			kept << rule;

	if (kept.size() != rules.size())
		AdmissionColumns::save(region, kept, &error);
}

auto CsvEditorDialog::photoColumn() const -> int {
	for (int column = 0; column < grid->columnCount(); ++column) {
		QTableWidgetItem *header = grid->horizontalHeaderItem(column);

		if (header && header->text() == QStringLiteral("照片"))
			return column;
	}

	return -1;
}

void CsvEditorDialog::setPhotoCell(int row, int column, const QString &path) {
	QTableWidgetItem *item = grid->item(row, column);

	if (! item) {
		item = new QTableWidgetItem();
		grid->setItem(row, column, item);
	}

	item->setText(path.trimmed());
	const QString full = resolvePhoto(region, path);
	item->setIcon(full.isEmpty() ? QIcon() : QIcon(full));
}

void CsvEditorDialog::choosePhotos() {
	const int column = grid->currentColumn();

	if (column < 0 || column != photoColumn())
		return;

	const QString path = QFileDialog::getOpenFileName(this, tr("Choose a photo"), QString(),
	                                                  tr("Images (*.png *.jpg *.jpeg *.bmp)"));

	if (path.isEmpty())
		return;

	// 能放进赛区目录就用相对路径（整个 admission 目录拷走也不会失效）
	const QString relative = QDir(AdmissionProject::regionFolder(region)).relativeFilePath(path);
	const QString value = QDir::fromNativeSeparators(relative.startsWith(QStringLiteral("..")) ? path : relative);

	QList<int> rows;

	for (const QTableWidgetSelectionRange &range : grid->selectedRanges())
		for (int row = range.topRow(); row <= range.bottomRow(); ++row)
			if (! rows.contains(row))
				rows << row;

	if (rows.isEmpty() && grid->currentRow() >= 0)
		rows << grid->currentRow();

	muted = true;

	for (int row : std::as_const(rows))
		if (row >= 0 && row < grid->rowCount())
			setPhotoCell(row, column, value);

	pushState(tr("Choose a photo"));
}

void CsvEditorDialog::renameColumn(int column) {
	if (column < 0)
		return;

	const QString old = grid->horizontalHeaderItem(column)->text();

	if (AdmissionTable::builtinColumns().contains(old)) {
		QMessageBox::information(this, tr("Admission tickets"),
		                         tr("The built-in columns cannot be renamed."));
		return;
	}

	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Rename column"), tr("Column name:"),
	                                          QLineEdit::Normal, old, &ok);

	if (! ok || name.trimmed().isEmpty() || name.trimmed() == old)
		return;

	store();
	table.header[column] = name.trimmed();
	load();
	pushState(tr("Rename a column"));
}

void CsvEditorDialog::headerMenu(const QPoint &position) {
	const int column = grid->horizontalHeader()->logicalIndexAt(position.x());

	if (column < 0)
		return;

	QMenu menu(this);
	QAction *fixed = menu.addAction(tr("Fill the column with a fixed value…"));
	QAction *sequence = menu.addAction(tr("Fill the column with a sequence…"));
	QAction *byRow = menu.addAction(tr("Fill the column with row numbers"));
	QAction *clear = menu.addAction(tr("Clear the column"));
	menu.addSeparator();
	QAction *add = menu.addAction(tr("Add column…"));
	QAction *rename = menu.addAction(tr("Rename column…"));
	QAction *remove = menu.addAction(tr("Remove column"));
	menu.addSeparator();
	QAction *rule = menu.addAction(tr("Configure this column…"));
	QAction *noRule = menu.addAction(tr("Remove the generation rule"));
	menu.addSeparator();
	QAction *stats = menu.addAction(tr("Column statistics…"));
	const QAction *chosen = menu.exec(grid->horizontalHeader()->mapToGlobal(position));

	if (! chosen)
		return;

	if (chosen == fixed)
		fillColumn(column, FillFixed);
	else if (chosen == sequence)
		fillColumn(column, FillSequence);
	else if (chosen == byRow)
		fillColumn(column, FillRowNumber);
	else if (chosen == clear)
		fillColumn(column, FillClear);
	else if (chosen == add)
		addColumn();
	else if (chosen == rename)
		renameColumn(column);
	else if (chosen == remove)
		removeColumns({column});
	else if (chosen == rule)
		configureColumn(column);
	else if (chosen == noRule)
		removeColumnRule(column);
	else if (chosen == stats)
		showColumnStatistics(column);
}

void CsvEditorDialog::configureColumn(int column) {
	if (column < 0)
		return;

	const QString name = grid->horizontalHeaderItem(column)->text().trimmed();
	QList<AdmissionColumnRule> rules;
	QString error;

	if (! AdmissionColumns::load(region, rules, &error) && ! error.isEmpty()) {
		QMessageBox::warning(this, tr("Admission tickets"), error);
		return;
	}

	AdmissionColumnRule existing;
	bool found = false;

	for (const AdmissionColumnRule &rule : std::as_const(rules))
		if (rule.name == name) {
			existing = rule;
			found = true;
		}

	store();
	ColumnDialog dialog(name, headerForValidation(), sampleContext(), found ? &existing : nullptr, this);

	if (dialog.exec() != QDialog::Accepted)
		return;

	const AdmissionColumnRule rule = dialog.rule();
	bool replaced = false;

	for (int index = 0; index < rules.size(); ++index)
		if (rules.at(index).name == name) {
			rules[index] = rule;
			replaced = true;
		}

	if (! replaced)
		rules << rule;

	if (! AdmissionColumns::save(region, rules, &error)) {
		QMessageBox::warning(this, tr("Admission tickets"), error);
		return;
	}

	// 配好就立刻按新规则算一遍，让用户马上看到效果。
	if (! AdmissionColumns::evaluate(rules, table, region, true, &error))
		QMessageBox::warning(this, tr("Admission tickets"), error);

	load();
	pushState(tr("Configure a column"));
}

void CsvEditorDialog::removeColumnRule(int column) {
	if (column < 0)
		return;

	const QString name = grid->horizontalHeaderItem(column)->text().trimmed();
	QList<AdmissionColumnRule> rules;
	QString error;

	if (! AdmissionColumns::load(region, rules, &error) && ! error.isEmpty()) {
		QMessageBox::warning(this, tr("Admission tickets"), error);
		return;
	}

	QList<AdmissionColumnRule> kept;

	for (const AdmissionColumnRule &rule : std::as_const(rules))
		if (rule.name != name)
			kept << rule;

	if (kept.size() == rules.size()) {
		QMessageBox::information(this, tr("Admission tickets"),
		                         tr("This column has no generation rule."));
		return;
	}

	if (! AdmissionColumns::save(region, kept, &error)) {
		QMessageBox::warning(this, tr("Admission tickets"), error);
		return;
	}

	load();
	pushState(tr("Remove a column rule"));
}

auto CsvEditorDialog::sampleContext() const -> AdmissionNamingContext {
	AdmissionNamingContext context;
	context.section = region;
	context.row = 1;
	context.col = 1;
	context.regionSeq = 1;
	context.globalSeq = 1;
	context.name = QStringLiteral("张三");
	context.id = (region.isEmpty() ? QStringLiteral("A") : region) + QStringLiteral("-S00001");
	context.seat = QStringLiteral("1");
	context.room = QStringLiteral("01");

	if (grid->rowCount() == 0)
		return context;

	// 用第一行的真实值做预览，占位符替换出来什么一看就知道。
	for (int column = 0; column < grid->columnCount(); ++column) {
		const QString label = grid->horizontalHeaderItem(column)->text().trimmed();
		const QString value = grid->item(0, column) ? grid->item(0, column)->text().trimmed() : QString();
		context.columns.insert(label, value);

		if (value.isEmpty())
			continue;

		if (label == QStringLiteral("姓名"))
			context.name = value;
		else if (label == QStringLiteral("准考证号"))
			context.id = value;
		else if (label == QStringLiteral("座位号"))
			context.seat = value;
		else if (label == QStringLiteral("考场"))
			context.room = value;
	}

	return context;
}

auto CsvEditorDialog::headerForValidation() const -> QStringList {
	QStringList columns;

	for (int column = 0; column < grid->columnCount(); ++column)
		columns << grid->horizontalHeaderItem(column)->text().trimmed();

	return columns;
}

void CsvEditorDialog::venuesClicked() {
	AdmissionRegion *item = project ? project->find(region) : nullptr;

	if (! item)
		return;

	VenueDialog dialog(item->name, item->venues, this);

	if (dialog.exec() != QDialog::Accepted)
		return;

	item->venues = dialog.venues();
	QString error;

	if (project && ! project->save(&error))
		QMessageBox::warning(this, tr("Admission tickets"), error);
}

void CsvEditorDialog::seatsClicked() {
	store();
	AdmissionRegion *item = project ? project->find(region) : nullptr;
	AdmissionAssign::SeatOptions options;
	options.layout = project ? project->seatLayout : AdmissionAssign::FillFirst;
	options.order = project ? project->seatOrder : QStringLiteral("row");

	SeatDialog dialog(table, item ? item->venues : QList<AdmissionVenue>(), options, this);

	// 「编辑考点 / 考场…」：就地改方案（唯一根据），改完刷新预览
	connect(&dialog, &SeatDialog::editVenuesRequested, this, [this, &dialog] {
		venuesClicked();

		if (const AdmissionRegion *changed = project ? project->find(region) : nullptr)
			dialog.setVenues(changed->venues);
	});

	if (dialog.exec() != QDialog::Accepted)
		return;

	options = dialog.options();

	if (project) {
		project->seatLayout = options.layout;
		project->seatOrder = options.order;
		QString saveError;

		if (! project->save(&saveError))
			QMessageBox::warning(this, tr("Admission tickets"), saveError);
	}

	QStringList venueValues;
	QStringList roomValues;
	QStringList seatValues;
	QString error;

	if (! AdmissionAssign::planSeats(table, item ? item->venues : QList<AdmissionVenue>(), options,
	                                 venueValues, roomValues, seatValues, &error)) {
		QMessageBox::warning(this, tr("Admission tickets"), error);
		return;
	}

	AdmissionAssign::applyColumn(table, QStringLiteral("考点"), venueValues);
	AdmissionAssign::applyColumn(table, QStringLiteral("考场"), roomValues);
	AdmissionAssign::applyColumn(table, QStringLiteral("座位号"), seatValues);
	load();
	pushState(tr("Assign seats"));
}

void CsvEditorDialog::idsClicked() {
	store();

	// 策略由用户在这里决定（模板 / 各组的取值来源 / 已有号码怎么办），选完存进 config.json。
	AdmissionAssign::IdOptions options;
	options.templateText = project ? project->idTemplate
	                              : QStringLiteral("<section>-S<number><number><number><number><number>");
	options.charSources = project ? project->idCharSources : QStringList();

	// 「全场序号」要把前面赛区的人数接上（模板里有 <char> 且选了它才用得到）
	if (project)
		for (const AdmissionRegion &item : project->regions) {
			if (item.name == region)
				break;

			options.globalOffset += item.table.rows.size();
		}

	IdRuleDialog dialog(table, region, options, this);

	if (dialog.exec() != QDialog::Accepted)
		return;

	options = dialog.options();

	if (project) {
		project->idTemplate = options.templateText;
		project->idCharSources = options.charSources;
		QString saveError;

		if (! project->save(&saveError))
			QMessageBox::warning(this, tr("Admission tickets"), saveError);
	}

	QStringList ids;
	QString error;

	if (! AdmissionAssign::planIds(table, region, options, ids, &error)) {
		QMessageBox::warning(this, tr("Admission tickets"), error);
		return;
	}

	AdmissionAssign::applyColumn(table, QStringLiteral("准考证号"), ids);
	load();
	pushState(tr("Generate ticket numbers"));
}

void CsvEditorDialog::gridMenu(const QPoint &position) {
	QMenu menu(this);
	QAction *fixed = menu.addAction(tr("Fill the selection with a fixed value…"));
	QAction *sequence = menu.addAction(tr("Fill the selection with a sequence…"));
	QAction *clear = menu.addAction(tr("Clear the selection"));
	QAction *photo = grid->currentColumn() == photoColumn() ? menu.addAction(tr("Choose a photo…")) : nullptr;
	menu.addSeparator();
	QAction *addRowAction = menu.addAction(tr("Add a row at the end"));
	QAction *addColumnAction = menu.addAction(tr("Add a column at the end"));
	QAction *removeRowsAction = menu.addAction(tr("Delete the selected rows"));
	QAction *removeColumnsAction = menu.addAction(tr("Delete the selected columns"));
	const QAction *chosen = menu.exec(grid->viewport()->mapToGlobal(position));

	if (! chosen)
		return;

	if (chosen == fixed)
		fillSelection(FillFixed);
	else if (chosen == sequence)
		fillSelection(FillSequence);
	else if (chosen == clear)
		fillSelection(FillClear);
	else if (photo && chosen == photo)
		choosePhotos();
	else if (chosen == addRowAction)
		addRowAtEnd();
	else if (chosen == addColumnAction)
		addColumn();
	else if (chosen == removeRowsAction)
		removeSelectedRows();
	else if (chosen == removeColumnsAction)
		removeSelectedColumns();
}

void CsvEditorDialog::validateClicked() {
	store();
	QStringList problems;
	const int nameColumn = table.columnIndex(QStringLiteral("姓名"));
	const int idColumn = table.columnIndex(QStringLiteral("准考证号"));
	const int seatColumn = table.columnIndex(QStringLiteral("座位号"));
	const int venueColumn = table.columnIndex(QStringLiteral("考点"));
	QMap<QString, int> seen;
	int firstBadRow = -1;

	for (int row = 0; row < table.rows.size(); ++row) {
		const QString name = table.cell(row, nameColumn);
		const QString id = table.cell(row, idColumn);
		const QString seat = table.cell(row, seatColumn);
		const QString venue = table.cell(row, venueColumn);

		if (venue.isEmpty()) {
			problems << tr("row %1: the test site is empty").arg(row + 2);
			firstBadRow = firstBadRow < 0 ? row : firstBadRow;
		}

		if (name.isEmpty()) {
			problems << tr("row %1: the name is empty").arg(row + 2);
			firstBadRow = firstBadRow < 0 ? row : firstBadRow;
		}

		if (seat.isEmpty()) {
			problems << tr("row %1: the seat is empty").arg(row + 2);
			firstBadRow = firstBadRow < 0 ? row : firstBadRow;
		}

		if (id.isEmpty()) {
			problems << tr("row %1: the ticket number is empty").arg(row + 2);
			firstBadRow = firstBadRow < 0 ? row : firstBadRow;
		} else if (seen.contains(id)) {
			problems << tr("row %1: the ticket number %2 is duplicated (row %3)")
		                 .arg(row + 2)
		                 .arg(id)
		                 .arg(seen.value(id) + 2);
			firstBadRow = firstBadRow < 0 ? row : firstBadRow;
		} else {
			seen.insert(id, row);
		}
	}

	if (problems.isEmpty()) {
		QMessageBox::information(this, tr("Admission tickets"),
		                         tr("Looks good: %1 contestants.").arg(table.rows.size()));
		return;
	}

	QMessageBox::warning(this, tr("Admission tickets"), problems.join(QChar('\n')));

	if (firstBadRow >= 0)
		grid->setCurrentCell(firstBadRow, qMax(0, nameColumn));
}
