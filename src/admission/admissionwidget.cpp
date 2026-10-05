/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "admissionwidget.h"
//
#include "admissiongenerator.h"
#include "admissionproject.h"
#include "csveditordialog.h"
#include "core/contest.h"
#include "core/contestant.h"
#include "notesdialog.h"
#include "venuedialog.h"
//
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <utility>

namespace {
	QString timestamp(const QFileInfo &info) {
		return info.exists() ? info.lastModified().toString(QStringLiteral("MM-dd HH:mm"))
		                     : QStringLiteral("—");
	}

	/// 某个赛区的产物目录（按赛区 / 按考场两种布局都算进来）。
	QString outputFolderFor(const AdmissionProject &project, const QString &region) {
		const QString root = AdmissionGenerator::outputRoot();

		if (project.dirLayout != QStringLiteral("byRegion") || region.isEmpty())
			return root;

		return root + QChar('/') + region;
	}

	int countPdfs(const QString &folder) {
		int count = 0;
		QDirIterator iterator(folder, {QStringLiteral("*.pdf")}, QDir::Files, QDirIterator::Subdirectories);

		while (iterator.hasNext()) {
			iterator.next();
			++count;
		}

		return count;
	}
} // namespace

AdmissionWidget::AdmissionWidget(QWidget *parent) : QWidget(parent) {
	project = new AdmissionProject();
	generator = new AdmissionGenerator(this);
	buildUi();

	connect(generator, &AdmissionGenerator::logMessage, this, &AdmissionWidget::appendLog);
	connect(generator, &AdmissionGenerator::progress, this, [this](int done, int total) {
		progress->setMaximum(qMax(1, total));
		progress->setValue(done);
		QApplication::processEvents();
	});

	updateButtons();
}

AdmissionWidget::~AdmissionWidget() {
	// 关窗口时可能刚好还在敲标题：析构前再存一次
	saveIfNeeded();
	delete project;
}

void AdmissionWidget::buildUi() {
	auto *layout = new QVBoxLayout(this);

	auto *textBar = new QHBoxLayout();
	titleLabel = new QLabel(this);
	titleEdit = new QLineEdit(this);
	titleEdit->setObjectName(QStringLiteral("titleEdit"));
	titleEdit->setMinimumWidth(200);
	examTimeLabel = new QLabel(this);
	examTimeEdit = new QLineEdit(this);
	examTimeEdit->setObjectName(QStringLiteral("examTimeEdit"));
	examTimeEdit->setMinimumWidth(160);
	notesButton = new QPushButton(this);
	contestNotesButton = new QPushButton(this);
	refreshButton = new QPushButton(this);
	textBar->addWidget(titleLabel);
	textBar->addWidget(titleEdit);
	textBar->addSpacing(12);
	textBar->addWidget(examTimeLabel);
	textBar->addWidget(examTimeEdit);
	textBar->addSpacing(12);
	textBar->addWidget(notesButton);
	textBar->addWidget(contestNotesButton);
	textBar->addStretch();
	textBar->addWidget(refreshButton);
	layout->addLayout(textBar);

	auto *fileBar = new QHBoxLayout();
	openFolderButton = new QPushButton(this);
	listButton = new QPushButton(this);
	venuesButton = new QPushButton(this);
	venuesButton->setObjectName(QStringLiteral("venuesButton"));
	newRegionButton = new QPushButton(this);
	deleteRegionButton = new QPushButton(this);
	fileBar->addWidget(listButton);
	fileBar->addWidget(venuesButton);
	fileBar->addWidget(newRegionButton);
	fileBar->addWidget(deleteRegionButton);
	fileBar->addStretch();
	fileBar->addWidget(openFolderButton);
	layout->addLayout(fileBar);

	auto *buildBar = new QHBoxLayout();
	layoutBox = new QComboBox(this);
	packageBox = new QComboBox(this);
	overwriteBox = new QCheckBox(this);
	overwriteBox->setChecked(true);
	buildSelectedButton = new QPushButton(this);
	buildAllButton = new QPushButton(this);
	stopButton = new QPushButton(this);
	openOutputButton = new QPushButton(this);
	buildBar->addWidget(layoutBox);
	buildBar->addWidget(packageBox);
	buildBar->addWidget(overwriteBox);
	buildBar->addStretch();
	buildBar->addWidget(buildSelectedButton);
	buildBar->addWidget(buildAllButton);
	buildBar->addWidget(stopButton);
	buildBar->addWidget(openOutputButton);
	layout->addLayout(buildBar);

	table = new QTableWidget(0, 4, this);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setContextMenuPolicy(Qt::CustomContextMenu);
	table->verticalHeader()->setVisible(false);
	table->horizontalHeader()->setStretchLastSection(true);
	table->setMinimumHeight(150);
	layout->addWidget(table, 1);

	progress = new QProgressBar(this);
	progress->setVisible(false);
	layout->addWidget(progress);

	log = new QPlainTextEdit(this);
	log->setObjectName(QStringLiteral("buildLog"));
	log->setReadOnly(true);
	log->setMaximumBlockCount(20000);
	log->setMinimumHeight(180);
	layout->addWidget(log);

	connect(refreshButton, &QPushButton::clicked, this, &AdmissionWidget::refreshClicked);

	// 输入框里敲完不一定会有 editingFinished（直接关掉程序就白敲了），所以停下来就落盘
	saveTimer = new QTimer(this);
	saveTimer->setSingleShot(true);
	saveTimer->setInterval(400);
	connect(saveTimer, &QTimer::timeout, this, &AdmissionWidget::saveTextEdits);
	connect(titleEdit, &QLineEdit::textChanged, this, &AdmissionWidget::startSaveTimer);
	connect(examTimeEdit, &QLineEdit::textChanged, this, &AdmissionWidget::startSaveTimer);
	connect(titleEdit, &QLineEdit::editingFinished, this, &AdmissionWidget::saveTextEdits);
	connect(examTimeEdit, &QLineEdit::editingFinished, this, &AdmissionWidget::saveTextEdits);
	connect(notesButton, &QPushButton::clicked, this, &AdmissionWidget::notesClicked);
	connect(contestNotesButton, &QPushButton::clicked, this, &AdmissionWidget::contestNotesClicked);
	connect(openFolderButton, &QPushButton::clicked, this, &AdmissionWidget::openFolderClicked);
	connect(listButton, &QPushButton::clicked, this, &AdmissionWidget::editListClicked);
	connect(venuesButton, &QPushButton::clicked, this, &AdmissionWidget::venuesClicked);
	connect(newRegionButton, &QPushButton::clicked, this, &AdmissionWidget::newRegionClicked);
	connect(deleteRegionButton, &QPushButton::clicked, this, &AdmissionWidget::deleteRegionClicked);
	connect(buildSelectedButton, &QPushButton::clicked, this, &AdmissionWidget::buildSelectedClicked);
	connect(buildAllButton, &QPushButton::clicked, this, &AdmissionWidget::buildAllClicked);
	connect(stopButton, &QPushButton::clicked, this, &AdmissionWidget::stopClicked);
	connect(openOutputButton, &QPushButton::clicked, this, &AdmissionWidget::openOutputClicked);
	connect(table, &QTableWidget::doubleClicked, this, &AdmissionWidget::editListClicked);
	connect(table, &QWidget::customContextMenuRequested, this, &AdmissionWidget::tableMenu);
	connect(table, &QTableWidget::itemSelectionChanged, this, &AdmissionWidget::updateButtons);
	connect(layoutBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &AdmissionWidget::optionsChanged);
	connect(packageBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &AdmissionWidget::optionsChanged);
	connect(overwriteBox, &QCheckBox::checkStateChanged, this, &AdmissionWidget::optionsChanged);

	retranslate();
}

void AdmissionWidget::retranslate() {
	titleLabel->setText(tr("Title:"));
	examTimeLabel->setText(tr("Test time:"));
	notesButton->setText(tr("Notes"));
	contestNotesButton->setText(tr("Contest notes"));
	refreshButton->setText(tr("Refresh"));
	openFolderButton->setText(tr("Open admission/"));
	listButton->setText(tr("Edit list"));
	venuesButton->setText(tr("Venues / rooms"));
	newRegionButton->setText(tr("New region"));
	deleteRegionButton->setText(tr("Delete region"));
	buildSelectedButton->setText(tr("Build selected"));
	buildAllButton->setText(tr("Build all"));
	stopButton->setText(tr("Stop"));
	openOutputButton->setText(tr("Open dist/admission/"));

	layoutBox->setItemText(0, tr("Folders: by region"));
	layoutBox->setItemText(1, tr("Folders: flat"));
	layoutBox->setItemText(2, tr("Folders: by room"));
	packageBox->setItemText(0, tr("Zip: by region"));
	packageBox->setItemText(1, tr("Zip: none"));
	overwriteBox->setText(tr("Overwrite"));

	table->setHorizontalHeaderLabels({tr("Region"), tr("List"), tr("Output"), tr("Zip")});
}

void AdmissionWidget::changeEvent(QEvent *event) {
	if (event->type() == QEvent::LanguageChange) {
		retranslate();
		rebuildTable();
	}

	QWidget::changeEvent(event);
}

void AdmissionWidget::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	refresh();
}

void AdmissionWidget::hideEvent(QHideEvent *event) {
	saveIfNeeded();
	QWidget::hideEvent(event);
}

void AdmissionWidget::setContest(Contest *value) {
	contest = value;
	reload();
}

void AdmissionWidget::setDayContext(const QString &fileName, const QString &title, const QString &contestTitleValue) {
	dayFileName = fileName;
	dayTitle = title;
	contestTitle = contestTitleValue;
}

void AdmissionWidget::refresh() { reload(); }

void AdmissionWidget::reload() {
	if (! contest) {
		// 比赛日关了：解除钉定，免得还往刚才那个目录里写
		AdmissionProject::useRoot(QString());
		return;
	}

	// 把 admission/ 钉成绝对路径：以后就算工作目录被别处改掉，也不会写错比赛日
	AdmissionProject::useRoot(QFileInfo(AdmissionProject::root()).absoluteFilePath());
	// 重新读盘前先把还在输入框里的标题 / 测试时间存下来，免得被回填覆盖掉
	if (saveTimer && saveTimer->isActive()) {
		saveTimer->stop();
		saveTextEdits();
	}

	// 下拉框没有条目时先补上（retranslate 只改文字）
	if (layoutBox->count() == 0) {
		layoutBox->addItem(QString(), QStringLiteral("byRegion"));
		layoutBox->addItem(QString(), QStringLiteral("flat"));
		layoutBox->addItem(QString(), QStringLiteral("byRoom"));
		packageBox->addItem(QString(), QStringLiteral("byRegion"));
		packageBox->addItem(QString(), QStringLiteral("none"));
		retranslate();
	}

	QStringList migrationLog;
	project->migrateLegacy(&migrationLog);

	for (const QString &line : std::as_const(migrationLog))
		appendLog(tr("migration: %1").arg(line));

	QString error;

	if (! project->load(contest->getRegionEnabled(), &error))
		appendLog(tr("Cannot load admission data: %1").arg(error));

	// 回填选项时不要把 optionsChanged 又跑一遍（会写入文件）
	QSignalBlocker layoutBlocker(layoutBox);
	QSignalBlocker packageBlocker(packageBox);
	QSignalBlocker overwriteBlocker(overwriteBox);

	if (layoutBox->findData(project->dirLayout) < 0)
		layoutBox->addItem(QString(), project->dirLayout);

	layoutBox->setCurrentIndex(qMax(0, layoutBox->findData(project->dirLayout)));
	packageBox->setCurrentIndex(qMax(0, packageBox->findData(project->packageByRegion
	                                                              ? QStringLiteral("byRegion")
	                                                              : QStringLiteral("none"))));
	overwriteBox->setChecked(project->overwrite);

	{
		// 标题留空就用比赛日标题（把比赛日标题当灰字提示）。
		QSignalBlocker blocker(titleEdit);
		titleEdit->setText(project->title);
		titleEdit->setPlaceholderText(dayTitle);
	}
	{
		QSignalBlocker blocker(examTimeEdit);
		examTimeEdit->setText(project->examTime);
	}

	rebuildTable();
}

void AdmissionWidget::rebuildTable() {
	table->setRowCount(0);
	groups.clear();

	for (const AdmissionRegion &region : project->regions)
		groups << region.name;

	for (const AdmissionRegion &region : project->regions) {
		const int row = table->rowCount();
		table->insertRow(row);
		const QString folder = outputFolderFor(*project, region.name);
		const int pdfs = countPdfs(folder);
		const QFileInfo newest(folder);

		table->setItem(row, 0, new QTableWidgetItem(region.name.isEmpty() ? tr("(no region)") : region.name));
		table->setItem(row, 1, new QTableWidgetItem(tr("%1 contestants").arg(region.table.rows.size())));
		table->setItem(row, 2, new QTableWidgetItem(pdfs > 0 ? tr("%1 PDFs · %2").arg(pdfs).arg(timestamp(newest))
		                                                    : QStringLiteral("—")));

		if (project->packageByRegion && ! region.name.isEmpty()) {
			const QFileInfo zip(folder + QStringLiteral(".zip"));
			table->setItem(row, 3, new QTableWidgetItem(timestamp(zip)));
		} else {
			table->setItem(row, 3, new QTableWidgetItem(QStringLiteral("—")));
		}
	}

	if (table->rowCount() > 0 && table->currentRow() < 0)
		table->setCurrentCell(0, 0);

	updateButtons();
}

auto AdmissionWidget::groupAt(int row) const -> QString { return groups.value(row); }

void AdmissionWidget::updateButtons() {
	const bool hasRow = table->currentRow() >= 0 && table->currentRow() < groups.size();
	listButton->setEnabled(hasRow && ! building);
	venuesButton->setEnabled(hasRow && ! building);
	deleteRegionButton->setEnabled(hasRow && ! building);
	notesButton->setEnabled(hasRow && ! building);
	contestNotesButton->setEnabled(contest && ! building);
	buildSelectedButton->setEnabled(hasRow && ! building);
	buildAllButton->setEnabled(! groups.isEmpty() && ! building);
	newRegionButton->setEnabled(contest && contest->getRegionEnabled() && ! building);
	openFolderButton->setEnabled(! building);
	stopButton->setEnabled(building);
}

void AdmissionWidget::refreshClicked() { reload(); }

void AdmissionWidget::openFolderClicked() {
	const QString dir = QFileInfo(AdmissionProject::root()).absoluteFilePath();
	QDir().mkpath(dir);
	QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void AdmissionWidget::openOutputClicked() {
	const QString dir = AdmissionGenerator::outputRoot();
	QDir().mkpath(dir);
	QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void AdmissionWidget::optionsChanged() {
	project->dirLayout = layoutBox->currentData().toString();
	project->packageByRegion = packageBox->currentData().toString() != QStringLiteral("none");
	project->overwrite = overwriteBox->isChecked();
	QString error;

	if (! project->save(&error))
		appendLog(tr("Cannot save admission config: %1").arg(error));

	rebuildTable();
}

void AdmissionWidget::editListClicked() {
	const int row = table->currentRow();
	AdmissionRegion *region = row >= 0 ? project->find(groupAt(row)) : nullptr;

	if (! region)
		return;

	// 名单编辑器下半部分是「注意事项」，点 OK 时它自己会落盘。
	CsvEditorDialog dialog(region->name, region->table, project, this);
	dialog.exec();
	rebuildTable();
}

void AdmissionWidget::deleteRegionClicked() {
	const int row = table->currentRow();

	if (row < 0)
		return;

	const QString region = groupAt(row);
	const QString label = region.isEmpty() ? tr("(no region)") : region;
	const QString question = region.isEmpty()
	                             ? tr("Delete the list and all of its settings?")
	                             : tr("Delete region \"%1\" and all of its files?").arg(region);

	if (QMessageBox::question(this, tr("Delete region"), question) != QMessageBox::Yes)
		return;

	QString error;

	if (! project->removeRegion(region, &error)) {
		QMessageBox::warning(this, tr("Admission tickets"), error);
		return;
	}

	appendLog(tr("deleted region %1").arg(label));
	reload();
}


void AdmissionWidget::tableMenu(const QPoint &position) {
	const int row = table->rowAt(position.y());

	if (row >= 0)
		table->setCurrentCell(row, 0);

	const bool hasRow = table->currentRow() >= 0 && table->currentRow() < groups.size();
	QMenu menu(this);
	QAction *list = menu.addAction(tr("Edit list"));
	QAction *notes = menu.addAction(tr("Notes"));
	QAction *venues = menu.addAction(tr("Venues / rooms…"));
	menu.addSeparator();
	QAction *add = menu.addAction(tr("New region"));
	QAction *remove = menu.addAction(tr("Delete region"));
	list->setEnabled(hasRow && ! building);
	notes->setEnabled(hasRow && ! building);
	venues->setEnabled(hasRow && ! building);
	remove->setEnabled(hasRow && ! building);
	add->setEnabled(contest && contest->getRegionEnabled() && ! building);
	const QAction *chosen = menu.exec(table->viewport()->mapToGlobal(position));

	if (! chosen)
		return;

	if (chosen == list)
		editListClicked();
	else if (chosen == notes)
		notesClicked();
	else if (chosen == venues)
		venuesClicked();
	else if (chosen == add)
		newRegionClicked();
	else if (chosen == remove)
		deleteRegionClicked();
}

void AdmissionWidget::venuesClicked() {
	const int row = table->currentRow();
	AdmissionRegion *region = row >= 0 ? project->find(groupAt(row)) : nullptr;

	if (! region)
		return;

	VenueDialog dialog(region->name, region->venues, this);

	if (dialog.exec() != QDialog::Accepted)
		return;

	region->venues = dialog.venues();
	QString error;

	if (! project->save(&error))
		appendLog(tr("Cannot save admission config: %1").arg(error));
}

void AdmissionWidget::startSaveTimer() {
	if (saveTimer)
		saveTimer->start();
}

void AdmissionWidget::saveIfNeeded() {
	if (saveTimer)
		saveTimer->stop();

	saveTextEdits();
}

void AdmissionWidget::saveTextEdits() {
	const QString title = titleEdit->text().trimmed();
	const QString examTime = examTimeEdit->text().trimmed();

	if (title == project->title && examTime == project->examTime)
		return;

	project->title = title;
	project->examTime = examTime;
	QString error;

	// 只动 config.json：标题 / 测试时间跟名单无关，别为了它把各赛区的 CSV 重写一遍
	if (! project->saveConfig(&error))
		appendLog(tr("Cannot save admission config: %1").arg(error));
	else
		appendLog(tr("title / test time saved → %1")
		              .arg(QDir::toNativeSeparators(AdmissionProject::configPath())));
}

void AdmissionWidget::notesClicked() {
	const int row = table->currentRow();
	AdmissionRegion *region = row >= 0 ? project->find(groupAt(row)) : nullptr;

	if (! region)
		return;

	const QString name = region->name.isEmpty() ? tr("(no region)") : region->name;
	NotesDialog dialog(tr("Notes — %1").arg(name), region->notes, this);

	if (dialog.exec() != QDialog::Accepted || dialog.text() == region->notes)
		return;

	region->notes = dialog.text();
	QString error;

	if (! project->save(&error))
		QMessageBox::warning(this, tr("Admission tickets"), error);
}

void AdmissionWidget::contestNotesClicked() {
	NotesDialog dialog(tr("Contest notes"), project->contestNotes, this);

	if (dialog.exec() != QDialog::Accepted || dialog.text() == project->contestNotes)
		return;

	project->contestNotes = dialog.text();
	QString error;

	if (! project->save(&error))
		QMessageBox::warning(this, tr("Admission tickets"), error);
}

void AdmissionWidget::newRegionClicked() {
	QStringList hints;

	if (contest)
		for (auto *contestant : contest->getContestantList())
			if (! contestant->getRegion().trimmed().isEmpty() &&
			    ! hints.contains(contestant->getRegion().trimmed()))
				hints << contestant->getRegion().trimmed();

	bool ok = false;
	const QString name = QInputDialog::getText(
	    this, tr("New region"),
	    hints.isEmpty() ? tr("Region folder name:")
	                    : tr("Region folder name (this contest has: %1):").arg(hints.join(QStringLiteral(", "))),
	    QLineEdit::Normal, QString(), &ok);

	if (! ok || name.trimmed().isEmpty())
		return;

	QString error;

	if (! project->createRegion(name.trimmed(), &error)) {
		QMessageBox::warning(this, tr("Admission tickets"), error);
		return;
	}

	project->load(contest && contest->getRegionEnabled(), nullptr);
	rebuildTable();
}

int AdmissionWidget::build(const QStringList &regions) {
	if (building)
		return -1;

	building = true;
	updateButtons();
	progress->setVisible(true);
	progress->setValue(0);
	QApplication::setOverrideCursor(Qt::WaitCursor);

	project->dirLayout = layoutBox->currentData().toString();
	project->packageByRegion = packageBox->currentData().toString() != QStringLiteral("none");
	project->overwrite = overwriteBox->isChecked();
	QString saveError;
	project->save(&saveError);

	// 每次生成都从头记：上一次的日志留着，会让人以为这次也生成了那些文件
	log->clear();
	appendLog(tr("— build started %1 · folders: %2 · packages: %3 —")
	              .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
	                   layoutBox->currentText(), packageBox->currentText()));
	appendLog(tr("regions: %1").arg(regions.isEmpty() ? tr("all") : regions.join(QStringLiteral(", "))));

	QElapsedTimer timer;
	timer.start();
	QString error;
	const int failed = generator->generate(*project, regions, dayTitle.isEmpty() ? contestTitle : dayTitle, &error);

	QApplication::restoreOverrideCursor();
	building = false;
	progress->setVisible(false);
	rebuildTable();

	if (failed < 0) {
		appendLog(tr("build failed: %1").arg(error));
		QMessageBox::warning(this, tr("Admission tickets"), error);
		return -1;
	}

	int total = 0;

	for (const AdmissionRegion &region : project->regions)
		if (regions.isEmpty() || regions.contains(region.name))
			total += region.table.rows.size();

	appendLog(tr("— done in %1 s · %2 of %3 generated · %4 failed —")
	              .arg(timer.elapsed() / 1000.0, 0, 'f', 1)
	              .arg(total - failed)
	              .arg(total)
	              .arg(failed));
	appendLog(tr("output: %1").arg(QDir::toNativeSeparators(AdmissionGenerator::outputRoot())));

	if (failed > 0)
		appendLog(tr("failed tickets keep their .tex / .log in the _failed folder."));

	return failed;
}

void AdmissionWidget::buildSelectedClicked() {
	const int row = table->currentRow();

	if (row < 0)
		return;

	build({groupAt(row)});
}

void AdmissionWidget::buildAllClicked() {
	build({});
}

void AdmissionWidget::stopClicked() {
	generator->cancel();
	appendLog(tr("Stopping after the current ticket…"));
}

void AdmissionWidget::appendLog(const QString &message) {
	log->appendPlainText(message);
}
