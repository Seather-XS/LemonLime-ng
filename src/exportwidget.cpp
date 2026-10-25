/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "exportwidget.h"
#include "base/settings.h"
#include "core/contest.h"
#include "core/packagebuilder.h"
#include "core/task.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
	class PackageItem : public QTreeWidgetItem {
	  public:
		using QTreeWidgetItem::QTreeWidgetItem;

		bool isFolder() const { return data(0, Qt::UserRole).toBool(); }
		void setFolder(bool value) { setData(0, Qt::UserRole, value); }

		bool operator<(const QTreeWidgetItem &other) const override {
			const bool left = isFolder();
			const bool right = other.data(0, Qt::UserRole).toBool();

			if (left != right)
				return left;

			return QString::compare(text(0), other.text(0), Qt::CaseInsensitive) < 0;
		}
	};

	bool sameIcon(const QIcon &left, const QIcon &right) {
		if (left.isNull() || right.isNull())
			return false;

		const QImage leftImage = left.pixmap(32).toImage();
		const QImage rightImage = right.pixmap(32).toImage();
		return ! leftImage.isNull() && leftImage == rightImage;
	}
} 

ExportWidget::ExportWidget(QWidget *parent) : QWidget(parent) {
	builder = new PackageBuilder(this);
	iconProvider = std::make_unique<QFileIconProvider>();
	buildUi();
	connect(builder, &PackageBuilder::logMessage, this, &ExportWidget::appendLog);

	refreshTimer = new QTimer(this);
	refreshTimer->setSingleShot(true);
	refreshTimer->setInterval(250);
	connect(refreshTimer, &QTimer::timeout, this, &ExportWidget::refresh);

	treeTimer = new QTimer(this);
	treeTimer->setSingleShot(true);
	treeTimer->setInterval(0);
	connect(treeTimer, &QTimer::timeout, this, &ExportWidget::buildTreeStep);

}

ExportWidget::~ExportWidget() = default;

void ExportWidget::buildUi() {
	auto *layout = new QVBoxLayout(this);

	auto *topBar = new QHBoxLayout();
	typeLabel = new QLabel(tr("Package type:"), this);
	topBar->addWidget(typeLabel);
	kindBox = new QComboBox(this);
	kindBox->addItems(PackageBuilder::kindNames());
	topBar->addWidget(kindBox);
	topBar->addStretch();
	refreshButton = new QPushButton(tr("Refresh"), this);
	refreshButton->setToolTip(tr("List the files that will be packed."));
	exportButton = new QPushButton(tr("Export .zip"), this);
	topBar->addWidget(refreshButton);
	topBar->addWidget(exportButton);
	layout->addLayout(topBar);

	auto *outputBar = new QHBoxLayout();
	outputTitleLabel = new QLabel(tr("Output:"), this);
	outputBar->addWidget(outputTitleLabel);
	outputLabel = new QLabel(this);
	outputLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	outputLabel->setStyleSheet(QStringLiteral("color: gray;"));
	outputBar->addWidget(outputLabel, 1);
	layout->addLayout(outputBar);

	optionsBox = new QGroupBox(tr("Options"), this);
	auto *optionsLayout = new QVBoxLayout(optionsBox);
	wrapBox = new QCheckBox(tr("Wrap everything in a folder named after the contest day file"), optionsBox);
	optionsLayout->addWidget(wrapBox);
	nestedBox = new QCheckBox(tr("Nest an inner zip named %1").arg(builder->innerArchiveName()), optionsBox);
	nestedBox->setToolTip(tr("The outer .zip will hold nothing but the inner zip; all the content lives inside it."));
	optionsLayout->addWidget(nestedBox);
	perTaskBox = new QCheckBox(tr("Give each task its own folder"), optionsBox);
	perTaskBox->setChecked(true);
	optionsLayout->addWidget(perTaskBox);
	structureBox = new QCheckBox(tr("Keep the original folder structure (data/, graders/, ...)"), optionsBox);
	structureBox->setChecked(true);
	optionsLayout->addWidget(structureBox);
	samplesBox = new QCheckBox(tr("Also export the sample data (down/)"), optionsBox);
	optionsLayout->addWidget(samplesBox);
	auto *statementRow = new QHBoxLayout();
	statementLabel = new QLabel(tr("Statement files:"), optionsBox);
	statementRow->addWidget(statementLabel, 0, Qt::AlignTop);
	auto *statementColumn = new QVBoxLayout();
	statementList = new QListWidget(optionsBox);
	statementList->setObjectName(QStringLiteral("statementList"));
	statementList->setMaximumHeight(96);
	statementList->setSelectionMode(QAbstractItemView::NoSelection);
	statementColumn->addWidget(statementList);
	auto *statementButtons = new QHBoxLayout();
	statementAllButton = new QPushButton(tr("Select all"), optionsBox);
	statementAllButton->setObjectName(QStringLiteral("statementAllButton"));
	statementNoneButton = new QPushButton(tr("Clear"), optionsBox);
	statementNoneButton->setObjectName(QStringLiteral("statementNoneButton"));
	for (auto *button : {statementAllButton, statementNoneButton}) {
		button->setFlat(true);
		button->setAutoDefault(false);
		statementButtons->addWidget(button);
	}

	statementButtons->addStretch();
	statementColumn->addLayout(statementButtons);
	statementRow->addLayout(statementColumn, 1);
	optionsLayout->addLayout(statementRow);
	encryptBox = new QCheckBox(tr("Encrypt with a password"), optionsBox);
	optionsLayout->addWidget(encryptBox);
	auto *passwordRow = new QHBoxLayout();
	passwordRow->addSpacing(24);
	passwordRow->addWidget(new QLabel(tr("Password:"), optionsBox));
	passwordEdit = new QLineEdit(optionsBox);
	passwordEdit->setEchoMode(QLineEdit::Password);
	passwordEdit->setEnabled(false);
	passwordRow->addWidget(passwordEdit, 1);
	showPasswordBox = new QCheckBox(tr("Show"), optionsBox);
	showPasswordBox->setEnabled(false);
	passwordRow->addWidget(showPasswordBox);
	optionsLayout->addLayout(passwordRow);
	layout->addWidget(optionsBox);

	contentsBox = new QGroupBox(tr("Package contents"), this);
	auto *contentsLayout = new QVBoxLayout(contentsBox);
	contentTree = new QTreeWidget(contentsBox);
	contentTree->setColumnCount(2);
	contentTree->setRootIsDecorated(true);
	contentTree->setUniformRowHeights(true);
	contentTree->setAnimated(true);
	contentTree->setSortingEnabled(true);
	contentTree->header()->setSortIndicatorShown(false);
	contentTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	contentTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	contentsLayout->addWidget(contentTree);
	layout->addWidget(contentsBox, 1);

	auto *statusBar = new QHBoxLayout();
	statusLabel = new QLabel(this);
	statusBar->addWidget(statusLabel);
	statusBar->addStretch();
	layout->addLayout(statusBar);

	logBox = new QGroupBox(tr("Export log"), this);
	auto *logLayout = new QVBoxLayout(logBox);
	logView = new QPlainTextEdit(logBox);
	logView->setReadOnly(true);
	logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	logView->setMaximumHeight(140);
	logLayout->addWidget(logView);
	layout->addWidget(logBox);

	connect(kindBox, &QComboBox::currentIndexChanged, this, [this] { kindChanged(); });
	connect(refreshButton, &QPushButton::clicked, this, [this] {
		builder->clearCache();
		refresh();
	});
	connect(exportButton, &QPushButton::clicked, this, &ExportWidget::exportClicked);
	connect(wrapBox, &QCheckBox::toggled, this, [this] { optionsChanged(); });
	connect(nestedBox, &QCheckBox::toggled, this, [this] { optionsChanged(); });
	connect(perTaskBox, &QCheckBox::toggled, this, &ExportWidget::perTaskToggled);
	connect(structureBox, &QCheckBox::toggled, this, [this] { optionsChanged(); });
	connect(samplesBox, &QCheckBox::toggled, this, &ExportWidget::samplesToggled);
	connect(statementList, &QListWidget::itemChanged, this, &ExportWidget::statementChanged);
	connect(statementAllButton, &QPushButton::clicked, this, [this] {
		statementSelectionCustomized = true;

		for (int i = 0; i < statementList->count(); i++)
			statementList->item(i)->setCheckState(Qt::Checked);
	});
	connect(statementNoneButton, &QPushButton::clicked, this, [this] {
		statementSelectionCustomized = true;

		for (int i = 0; i < statementList->count(); i++)
			statementList->item(i)->setCheckState(Qt::Unchecked);
	});
	connect(encryptBox, &QCheckBox::toggled, this, [this] { optionsChanged(); });
	connect(passwordEdit, &QLineEdit::textChanged, this, &ExportWidget::passwordChanged);
	connect(showPasswordBox, &QCheckBox::toggled, this, [this] {
		passwordEdit->setEchoMode(showPasswordBox->isChecked() ? QLineEdit::Normal : QLineEdit::Password);
	});

	retranslate();
}

void ExportWidget::retranslate() {
	typeLabel->setText(tr("Package type:"));
	outputTitleLabel->setText(tr("Output:"));
	optionsBox->setTitle(tr("Options"));
	wrapBox->setText(tr("Wrap everything in a folder named after the contest day file"));
	wrapBox->setToolTip(tr("Adds one more level: the .zip contains a <day>/ folder holding everything."));
	nestedBox->setToolTip(
	    tr("The outer .zip will hold nothing but the inner zip; all the content lives inside it."));
	perTaskBox->setText(tr("Give each task its own folder"));
	structureBox->setText(tr("Keep the original folder structure (data/, graders/, ...)"));
	samplesBox->setText(tr("Also export the sample data (down/)"));
	samplesBox->setToolTip(tr("The sample data is taken from each task's down/ folder, keeping the structure."));
	statementLabel->setText(tr("Statement files:"));
	statementList->setToolTip(tr("Which files under statement/ go into the package. Check none to leave the "
	                             "statement out (the PDF name is set in the Statement tab)."));
	statementAllButton->setText(tr("Select all"));
	statementNoneButton->setText(tr("Clear"));
	encryptBox->setText(tr("Encrypt with a password"));
	showPasswordBox->setText(tr("Show"));
	contentsBox->setTitle(tr("Package contents"));
	logBox->setTitle(tr("Export log"));
	refreshButton->setText(tr("Refresh"));
	refreshButton->setToolTip(tr("List the files that will be packed."));
	exportButton->setText(tr("Export .zip"));
	contentTree->setHeaderLabels({tr("Path in package"), tr("Source on disk")});

	syncOptionWidgets();
}

void ExportWidget::syncOptionWidgets() {
	const bool testData = builder->kind() == PackageBuilder::TestDataPackage;
	const bool answers = builder->kind() == PackageBuilder::AnswersPackage;

	perTaskBox->setVisible(testData);
	structureBox->setVisible(testData);
	samplesBox->setVisible(testData);
	const bool showStatement = ! testData && ! answers;
	statementLabel->setVisible(showStatement);
	statementList->setVisible(showStatement);
	statementAllButton->setVisible(showStatement);
	statementNoneButton->setVisible(showStatement);

	if (answers) {
		wrapBox->setText(tr("Also wrap everything in a folder named after the contest day file"));
		wrapBox->setToolTip(tr("Gives <day>/ and, when regions are enabled, <day>/<region>/ for every region."));
		nestedBox->setText(tr("Give every region its own inner zip"));
		nestedBox->setToolTip(tr("The outer answers.zip will also hold one <region>.zip per region."));
	} else {
		wrapBox->setText(tr("Wrap everything in a folder named after the contest day file"));
		wrapBox->setToolTip(tr("Adds one more level: the .zip contains a <day>/ folder holding everything."));
		nestedBox->setText(tr("Nest an inner zip named %1").arg(builder->innerArchiveName()));
		nestedBox->setToolTip(
		    tr("The outer .zip will hold nothing but the inner zip; all the content lives inside it."));
	}

	if (! testData)
		return;

	{
		QSignalBlocker blocker(perTaskBox);

		if (duplicatesLocked)
			perTaskBox->setChecked(true);

		perTaskBox->setEnabled(! duplicatesLocked);
		perTaskBox->setToolTip(duplicatesLocked
		                           ? tr("Some tasks use the same data file names, so every task must "
		                                "keep its own folder.")
		                           : tr("Each task gets its own folder inside the package."));
	}

	{
		QSignalBlocker blocker(structureBox);

		if (samplesBox->isChecked())
			structureBox->setChecked(true);

		structureBox->setEnabled(! samplesBox->isChecked());
		structureBox->setToolTip(samplesBox->isChecked()
		                             ? tr("The sample data keeps the folder structure, so this cannot be "
		                                  "turned off.")
		                             : tr("Keeps data/, graders/ and down/ inside each task folder."));
	}
}

void ExportWidget::changeEvent(QEvent *event) {
	if (event->type() == QEvent::LanguageChange) {
		const int index = kindBox->currentIndex();
		QSignalBlocker blocker(kindBox);
		kindBox->clear();
		kindBox->addItems(PackageBuilder::kindNames());
		kindBox->setCurrentIndex(index);
		retranslate();
		refresh();
	}

	QWidget::changeEvent(event);
}

void ExportWidget::setContest(Contest *contest) {
	curContest = contest;
	scheduleRefresh();
}

void ExportWidget::setDayFile(const QString &filePath) {
	dayFile = filePath;
	builder->setDayFile(filePath);
	scheduleRefresh();
}

void ExportWidget::setDefaultStatementFile(const QString &fileName) {
	const QString name = QFileInfo(fileName).fileName();

	if (name.isEmpty() || name == defaultStatementFile)
		return;

	defaultStatementFile = name;

	if (! statementSelectionCustomized)
		statementSelectionInitialized = false;

	reloadStatementFiles();
	syncOptions();
	scheduleRefresh();
}

static QString statementNameOf(const QString &text, const QString &missingSuffix) {
	const int marker = text.indexOf(missingSuffix);
	return marker < 0 ? text : text.left(marker).trimmed();
}

void ExportWidget::reloadStatementFiles() {
	const QStringList previous = statementSelectionInitialized ? checkedStatementFiles() : QStringList();
	const QDir statementDir(QDir(builder->dayRoot()).absoluteFilePath(Settings::statementPath()));
	QStringList names;

	if (statementDir.exists()) {
		const QFileInfoList found =
		    statementDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

		for (const QFileInfo &info : found)
			names << info.fileName();
	}

	std::sort(names.begin(), names.end(), [](const QString &left, const QString &right) {
		const bool leftPdf = left.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive);
		const bool rightPdf = right.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive);

		if (leftPdf != rightPdf)
			return leftPdf;

		return QString::compare(left, right, Qt::CaseInsensitive) < 0;
	});

	const QString fallback = defaultStatementFile;
	const QString missingSuffix = QStringLiteral(" (") + tr("missing") + QChar(')');

	if (! fallback.isEmpty() && ! names.contains(fallback))
		names.prepend(fallback + missingSuffix);

	QSignalBlocker blocker(statementList);
	statementList->clear();

	for (const QString &name : names) {
		auto *item = new QListWidgetItem(name, statementList);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		const QString real = statementNameOf(name, missingSuffix);
		const bool checked = statementSelectionInitialized ? previous.contains(real)
		                                                   : real == fallback;
		item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
	}

	statementSelectionInitialized = true;
	builder->setStatementFiles(checkedStatementFiles());
}

auto ExportWidget::checkedStatementFiles() const -> QStringList {
	const QString missingSuffix = QStringLiteral(" (") + tr("missing") + QChar(')');
	QStringList files;

	for (int row = 0; row < statementList->count(); row++) {
		const QListWidgetItem *item = statementList->item(row);

		if (item->checkState() != Qt::Checked)
			continue;

		const QString name = statementNameOf(item->text(), missingSuffix);

		if (! name.isEmpty() && ! files.contains(name))
			files.append(name);
	}

	return files;
}

auto ExportWidget::missingStatementFiles() const -> QStringList {
	const QDir statementDir(QDir(builder->dayRoot()).absoluteFilePath(Settings::statementPath()));
	QStringList missing;

	for (const QString &name : checkedStatementFiles())
		if (! QFileInfo(statementDir.absoluteFilePath(name)).isFile())
			missing.append(name);

	return missing;
}

void ExportWidget::statementChanged() {
	statementSelectionCustomized = true;
	builder->setStatementFiles(checkedStatementFiles());
	scheduleRefresh();
}

void ExportWidget::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);

	if (needsRefresh)
		scheduleRefresh();
}

void ExportWidget::scheduleRefresh() {
	needsRefresh = true;

	if (! isVisible())
		return;

	refreshTimer->start();
}

void ExportWidget::appendLog(const QString &line) {
	logView->appendPlainText(line);
}

void ExportWidget::kindChanged() {
	builder->setKind(PackageBuilder::kindAt(kindBox->currentIndex()));
	scheduleRefresh();
}

void ExportWidget::optionsChanged() {
	const bool encrypt = encryptBox->isChecked();
	passwordEdit->setEnabled(encrypt);
	showPasswordBox->setEnabled(encrypt);
	syncOptions();
	scheduleRefresh();
}

void ExportWidget::passwordChanged() { syncOptions(); }

void ExportWidget::perTaskToggled() {
	if (! perTaskBox->isChecked() && builder->hasDuplicateTaskFiles()) {
		QMessageBox::warning(this, tr("Export"),
		                     tr("Some tasks use the same data file names, so they cannot share one folder."),
		                     QMessageBox::Ok);
		QSignalBlocker blocker(perTaskBox);
		perTaskBox->setChecked(true);
	}

	optionsChanged();
}

void ExportWidget::samplesToggled() { optionsChanged(); }

void ExportWidget::syncOptions() {
	builder->setWrapInFolder(wrapBox->isChecked());
	builder->setNestedZip(nestedBox->isChecked());
	builder->setOneFolderPerTask(perTaskBox->isChecked());
	builder->setKeepStructure(structureBox->isChecked());
	builder->setIncludeSamples(samplesBox->isChecked());
	builder->setStatementFiles(checkedStatementFiles());
	builder->setEncryptionMethod(PackageBuilder::Aes256Encryption);
	builder->setPassword(encryptBox->isChecked() ? passwordEdit->text() : QString());
}

auto ExportWidget::folderNode(QTreeWidgetItem *root, const QString &path) -> QTreeWidgetItem * {
	if (path.isEmpty())
		return root;

	QHash<QString, QTreeWidgetItem *> &nodes = folderNodes[root];

	if (nodes.contains(path))
		return nodes.value(path);

	const int slash = path.lastIndexOf(QChar('/'));
	const QString parentPath = slash < 0 ? QString() : path.left(slash);
	const QString name = slash < 0 ? path : path.mid(slash + 1);

	QTreeWidgetItem *parent = folderNode(root, parentPath);
	auto *node = parent ? new PackageItem(parent, {name, QString()})
	                    : new PackageItem(contentTree, {name, QString()});
	node->setFolder(true);
	node->setIcon(0, folderIcon);
	nodes.insert(path, node);
	++folderTotal;
	return node;
}

auto ExportWidget::archiveNode(const QString &path) -> QTreeWidgetItem * {
	const int slash = path.lastIndexOf(QChar('/'));
	const QString parentPath = slash < 0 ? QString() : path.left(slash);
	const QString name = slash < 0 ? path : path.mid(slash + 1);
	QTreeWidgetItem *parent = folderNode(nullptr, parentPath);
	auto *node = parent ? new PackageItem(parent, {name, QString()})
	                    : new PackageItem(contentTree, {name, QString()});
	node->setFolder(false);
	node->setIcon(0, fileIconFor(name));
	return node;
}

auto ExportWidget::fileIconFor(const QString &fileName) -> QIcon {
	const QString suffix = QFileInfo(fileName).suffix().toLower();
	auto cached = fileTypeIcons.find(suffix);

	if (cached != fileTypeIcons.end())
		return cached.value();

	QIcon icon;

	if (! suffix.isEmpty() && iconProvider)
		icon = iconProvider->icon(QFileInfo(QStringLiteral("lemon.") + suffix));

	if (icon.isNull() || sameIcon(icon, unknownFileIcon))
		icon = fallbackFileIcon;

	fileTypeIcons.insert(suffix, icon);
	return icon;
}

void ExportWidget::buildTreeStep() {
	constexpr int perStep = 500;
	int done = 0;

	while (pendingIndex < pendingNodes.size() && done < perStep) {
		const PendingNode &pending = pendingNodes.at(pendingIndex);
		const int slash = pending.path.lastIndexOf(QChar('/'));

		if (pending.folder) {
			folderNode(pending.root, pending.path);
		} else {
			QTreeWidgetItem *parent = folderNode(pending.root, slash < 0 ? QString() : pending.path.left(slash));
			const QString name = slash < 0 ? pending.path : pending.path.mid(slash + 1);
			auto *node = parent ? new PackageItem(parent, {name, QString()})
			                    : new PackageItem(contentTree, {name, QString()});
			node->setFolder(false);
			node->setIcon(0, fileIconFor(name));
			node->setText(1, QDir::toNativeSeparators(pending.source));
			++fileTotal;
		}

		++pendingIndex;
		++done;
	}

	if (pendingIndex < pendingNodes.size()) {
		statusLabel->setText(tr("Listing... %1 file(s)").arg(fileTotal));
		treeTimer->start();
		return;
	}

	finishTree();
}

void ExportWidget::finishTree() {
	pendingNodes.clear();
	pendingIndex = 0;
	statusLabel->setText(tr("%1 file(s), %2 folder(s)").arg(fileTotal).arg(folderTotal));
	exportButton->setEnabled(fileTotal > 0);

	contentTree->setSortingEnabled(true);

	contentTree->collapseAll();
}

void ExportWidget::refresh() {
	refreshTimer->stop();
	needsRefresh = false;
	treeTimer->stop();
	pendingNodes.clear();
	pendingIndex = 0;
	folderNodes.clear();
	fileTotal = 0;
	folderTotal = 0;
	contentTree->clear();
	logView->clear();

	if (! curContest) {
		outputLabel->clear();
		statusLabel->setText(tr("No contest"));
		exportButton->setEnabled(false);
		syncOptionWidgets();
		return;
	}

	unknownFileIcon = iconProvider->icon(QFileIconProvider::File);
	fallbackFileIcon = QIcon(QStringLiteral(":/icon/file.svg"));

	if (fallbackFileIcon.pixmap(16).isNull())
		fallbackFileIcon = contentTree->style()->standardIcon(QStyle::SP_FileIcon);

	fileTypeIcons.clear();
	folderIcon = contentTree->style()->standardIcon(QStyle::SP_DirIcon);
	contentTree->setSortingEnabled(false);

	syncOptions();
	builder->setContest(curContest);
	duplicatesLocked =
	    builder->kind() == PackageBuilder::TestDataPackage && builder->hasDuplicateTaskFiles();
	syncOptionWidgets();
	reloadStatementFiles();
	syncOptions();

	builder->setOutputFile(builder->defaultOutputFile());
	outputLabel->setText(QDir::toNativeSeparators(builder->defaultOutputFile()));

	const PackageBuilder::Plan plan = builder->collectItems();

	for (const PackageBuilder::Item &item : plan.items) {
		PendingNode pending;
		pending.root = nullptr;
		pending.path = item.archivePath;
		pending.source = item.sourcePath;
		pending.folder = item.isDirectory;
		pendingNodes.append(pending);
	}

	for (const PackageBuilder::NestedArchive &archive : plan.archives) {
		QTreeWidgetItem *root = archiveNode(archive.name);

		for (const PackageBuilder::Item &item : archive.items) {
			PendingNode pending;
			pending.root = root;
			pending.path = item.archivePath;
			pending.source = item.sourcePath;
			pending.folder = item.isDirectory;
			pendingNodes.append(pending);
		}
	}

	exportButton->setEnabled(! pendingNodes.isEmpty());

	if (pendingNodes.isEmpty()) {
		finishTree();
		return;
	}

	buildTreeStep();

	if (builder->kind() == PackageBuilder::ContestantPackage) {
		const QStringList missing = missingStatementFiles();

		if (! missing.isEmpty())
			appendLog(tr("These statement files are missing; export them in the Statement tab first if "
			             "you need them: %1")
			              .arg(missing.join(QStringLiteral(", "))));
	}
}

void ExportWidget::exportClicked() {
	if (! curContest)
		return;

	syncOptions();

	if (encryptBox->isChecked() && passwordEdit->text().isEmpty()) {
		QMessageBox::warning(this, tr("Export"), tr("Please type a password first."), QMessageBox::Ok);
		return;
	}

	refresh();

	const QString target = builder->defaultOutputFile();
	const QStringList missing = builder->kind() == PackageBuilder::ContestantPackage
	                                ? missingStatementFiles()
	                                : QStringList();

	if (! missing.isEmpty()) {
		const auto answer = QMessageBox::question(
		    this, tr("Export"),
		    tr("These statement files do not exist yet, so the package will not contain them:\n%1\n"
		       "Export anyway?")
		        .arg(missing.join(QChar('\n'))),
		    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

		if (answer != QMessageBox::Yes)
			return;
	}

	QApplication::setOverrideCursor(Qt::WaitCursor);
	const bool ok = builder->build();
	QApplication::restoreOverrideCursor();

	if (! ok) {
		QMessageBox::warning(this, tr("Export"), tr("Export failed: %1").arg(builder->lastError()),
		                     QMessageBox::Ok);
		refresh();
		return;
	}

	QMessageBox::information(this, tr("Export"),
	                         tr("Export is done") + QChar('\n') + QDir::toNativeSeparators(target),
	                         QMessageBox::Ok);
}
