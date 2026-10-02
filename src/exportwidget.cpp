/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "exportwidget.h"
//
#include "base/settings.h"
#include "core/contest.h"
#include "core/packagebuilder.h"
#include "core/task.h"
//
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
//
#include <algorithm>

namespace {
	/// 树节点：记住自己是不是目录，排序时目录排在文件前面（和资源管理器一样）。
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

	/// 按包内路径逐级建目录节点，把条目挂进树里（像文件资源管理器那样嵌套）。
	/// 具体插入由 ExportWidget 分批驱动（见 buildTreeStep()），这里只提供节点类型。
} // namespace

ExportWidget::ExportWidget(QWidget *parent) : QWidget(parent) {
	builder = new PackageBuilder(this);
	buildUi();
	connect(builder, &PackageBuilder::logMessage, this, &ExportWidget::appendLog);

	// 改选项时不要每一下都重建整棵树：攒一下再刷新。
	refreshTimer = new QTimer(this);
	refreshTimer->setSingleShot(true);
	refreshTimer->setInterval(250);
	connect(refreshTimer, &QTimer::timeout, this, &ExportWidget::refresh);

	// 预览树分批建：一次插上千个节点会先把界面卡住几秒，所以让出一帧再继续。
	treeTimer = new QTimer(this);
	treeTimer->setSingleShot(true);
	treeTimer->setInterval(0);
	connect(treeTimer, &QTimer::timeout, this, &ExportWidget::buildTreeStep);

	// 这里不调 refresh()：此时还没打开任何比赛日，QDir::currentPath() 不是比赛日目录。
}

ExportWidget::~ExportWidget() = default;

void ExportWidget::buildUi() {
	auto *layout = new QVBoxLayout(this);

	// ---- 顶部：包类型 + 输出位置 ----
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

	hintLabel = new QLabel(this);
	hintLabel->setWordWrap(true);
	hintLabel->setStyleSheet(QStringLiteral("color: gray;"));
	layout->addWidget(hintLabel);

	auto *outputBar = new QHBoxLayout();
	outputTitleLabel = new QLabel(tr("Output:"), this);
	outputBar->addWidget(outputTitleLabel);
	outputLabel = new QLabel(this);
	outputLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	// 输出路径固定为 <比赛日>/dist/export/<比赛日>.zip，不给改。
	outputLabel->setStyleSheet(QStringLiteral("color: gray;"));
	outputBar->addWidget(outputLabel, 1);
	layout->addLayout(outputBar);

	// ---- 打包选项 ----
	optionsBox = new QGroupBox(tr("Options"), this);
	auto *optionsLayout = new QVBoxLayout(optionsBox);
	// 提示文字统一由 retranslate() 负责，免得出现两个只差几个字的句子要翻译两遍。
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
	// 题面文件：只在「选手目录」包里才有意义（题面文件名不一定叫 statement.pdf）。
	auto *statementRow = new QHBoxLayout();
	statementLabel = new QLabel(tr("Statement file:"), optionsBox);
	statementRow->addWidget(statementLabel);
	statementBox = new QComboBox(optionsBox);
	statementBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	statementBox->setMinimumContentsLength(24);
	statementRow->addWidget(statementBox, 1);
	optionsLayout->addLayout(statementRow);
	encryptBox = new QCheckBox(tr("Encrypt with a password (ZipCrypto)"), optionsBox);
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

	// ---- 中间：打包内容预览 ----
	contentsBox = new QGroupBox(tr("Package contents"), this);
	auto *contentsLayout = new QVBoxLayout(contentsBox);
	contentTree = new QTreeWidget(contentsBox);
	contentTree->setColumnCount(2);
	contentTree->setRootIsDecorated(true);
	contentTree->setUniformRowHeights(true);
	contentTree->setAnimated(true);
	// 目录在前、同类按名字排序，排序交给 PackageItem::operator<（和资源管理器一致）
	contentTree->setSortingEnabled(true);
	contentTree->header()->setSortIndicatorShown(false);
	contentTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	contentTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	contentsLayout->addWidget(contentTree);
	layout->addWidget(contentsBox, 1);

	// ---- 底部：状态 + 日志 ----
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
		// 手动刷新时强制重新读一遍磁盘（数据可能刚改过）。
		builder->clearCache();
		refresh();
	});
	connect(exportButton, &QPushButton::clicked, this, &ExportWidget::exportClicked);
	connect(wrapBox, &QCheckBox::toggled, this, [this] { optionsChanged(); });
	connect(nestedBox, &QCheckBox::toggled, this, [this] { optionsChanged(); });
	connect(perTaskBox, &QCheckBox::toggled, this, &ExportWidget::perTaskToggled);
	connect(structureBox, &QCheckBox::toggled, this, [this] { optionsChanged(); });
	connect(samplesBox, &QCheckBox::toggled, this, &ExportWidget::samplesToggled);
	connect(statementBox, &QComboBox::currentTextChanged, this, &ExportWidget::statementChanged);
	connect(encryptBox, &QCheckBox::toggled, this, [this] { optionsChanged(); });
	// 密码不影响打包内容，只是存在 builder 里，不必重建树（否则每敲一个字符都重算一遍）。
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
	statementLabel->setText(tr("Statement file:"));
	statementBox->setToolTip(tr("Which file under statement/ goes into the package (the PDF name is set in the "
	                           "Statement tab)."));
	encryptBox->setText(tr("Encrypt with a password (ZipCrypto)"));
	showPasswordBox->setText(tr("Show"));
	contentsBox->setTitle(tr("Package contents"));
	logBox->setTitle(tr("Export log"));
	refreshButton->setText(tr("Refresh"));
	refreshButton->setToolTip(tr("List the files that will be packed."));
	exportButton->setText(tr("Export .zip"));
	contentTree->setHeaderLabels({tr("Path in package"), tr("Source on disk")});

	syncOptionWidgets();
}

// 按当前包类型显示 / 启用选项，并更新内层压缩包名字与说明文字。
void ExportWidget::syncOptionWidgets() {
	const bool testData = builder->kind() == PackageBuilder::TestDataPackage;
	const bool answers = builder->kind() == PackageBuilder::AnswersPackage;

	perTaskBox->setVisible(testData);
	structureBox->setVisible(testData);
	samplesBox->setVisible(testData);
	statementLabel->setVisible(! testData && ! answers);
	statementBox->setVisible(! testData && ! answers);

	// 名字与说明都随包类型变。
	if (answers) {
		wrapBox->setText(tr("Also wrap everything in a folder named after the contest day file"));
		wrapBox->setToolTip(tr("Gives <day>/ and, when regions are enabled, <day>/<region>/ for every region."));
		nestedBox->setText(tr("Give every region its own inner zip"));
		nestedBox->setToolTip(tr("The outer answers.zip will also hold one <region>.zip per region."));
		hintLabel->setText(tr("Answers of every contestant: one folder per contestant with all of their "
		                      "source files. With regions enabled every region always gets its own folder "
		                      "(and can additionally be packed into a <region>.zip). The <day>/ wrapper is "
		                      "optional."));
	} else {
		wrapBox->setText(tr("Wrap everything in a folder named after the contest day file"));
		wrapBox->setToolTip(tr("Adds one more level: the .zip contains a <day>/ folder holding everything."));
		nestedBox->setText(tr("Nest an inner zip named %1").arg(builder->innerArchiveName()));
		nestedBox->setToolTip(
		    tr("The outer .zip will hold nothing but the inner zip; all the content lives inside it."));
		hintLabel->setText(testData
		                       ? tr("Test data of every task: the whole <b>data/</b> folder. From <b>graders/</b> "
		                            "only the files the task really needs are packed (the interactive library, "
		                            "or the checker source of a special judged task). <b>gen/</b> and "
		                            "<b>tests/</b> are never packed, and neither is the statement PDF. "
		                            "<b>down/</b> is added when the sample data is included (only the files "
		                            "directly inside it; subfolders are skipped).")
		                       : tr("One subfolder per task (named after the task's folder), containing the files "
		                            "directly inside that task's <b>down/</b> folder (subfolders are skipped). The "
		                            "statement file selected below is placed in the zip root."));
	}

	if (! testData)
		return;

	// 各题文件重名时就只能每道题单独一个目录，把勾选框锁住（重名与否在 refresh() 里算好了）。
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

	// 勾了「一并导出样例数据」就必须保留结构。
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

// 题面选项卡算出来的默认题面文件名：下拉框里没有就补一项，并选上它。
void ExportWidget::setDefaultStatementFile(const QString &fileName) {
	const QString name = QFileInfo(fileName).fileName();

	if (name.isEmpty() || name == defaultStatementFile)
		return;

	defaultStatementFile = name;
	reloadStatementFiles();
	syncOptions();
	scheduleRefresh();
}

// 列出 statement/ 下的文件：选中的保持选中，否则选默认题面（即使它还没被导出）。
void ExportWidget::reloadStatementFiles() {
	const QString current = statementBox->currentText();
	const QDir statementDir(QDir(builder->dayRoot()).absoluteFilePath(Settings::statementPath()));
	QStringList names;

	if (statementDir.exists()) {
		const QFileInfoList found =
		    statementDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

		for (const QFileInfo &info : found)
			names << info.fileName();
	}

	const QString fallback = defaultStatementFile.isEmpty() ? builder->statementFile() : defaultStatementFile;

	// PDF 排前面（题面通常就是 PDF），同类按名字排。
	std::sort(names.begin(), names.end(), [](const QString &left, const QString &right) {
		const bool leftPdf = left.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive);
		const bool rightPdf = right.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive);

		if (leftPdf != rightPdf)
			return leftPdf;

		return QString::compare(left, right, Qt::CaseInsensitive) < 0;
	});

	// 默认题面可能还没导出：补一项（标上「未找到」）让用户看得见。
	if (! fallback.isEmpty() && ! names.contains(fallback))
		names.prepend(fallback + QStringLiteral(" (") + tr("missing") + QStringLiteral(")"));

	QSignalBlocker blocker(statementBox);
	statementBox->clear();
	statementBox->addItems(names);

	if (names.contains(current))
		statementBox->setCurrentText(current);
	else if (! names.isEmpty())
		statementBox->setCurrentIndex(0);

	// 建 builder 里存的是真文件名，「(missing)」这种标记不能带进去。
	builder->setStatementFile(selectedStatementFile());
}

// 下拉框当前项对应的真文件名（去掉「 (missing)」这种装饰）。
QString ExportWidget::selectedStatementFile() const {
	const QString text = statementBox->currentText();
	const int marker = text.indexOf(QStringLiteral(" ("));
	return marker < 0 ? text : text.left(marker);
}

void ExportWidget::statementChanged() {
	builder->setStatementFile(selectedStatementFile());
	scheduleRefresh();
}

// 打开比赛日时这个选项卡通常还在后台：先不遍历磁盘，等用户切过来再算。
void ExportWidget::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);

	// 延迟一点：先把选项卡画出来，再做需要扫目录的重活。
	if (needsRefresh)
		scheduleRefresh();
}

void ExportWidget::scheduleRefresh() {
	needsRefresh = true;

	// 没显示出来就不算（列整份数据可能要遍历上万个文件）。
	if (! isVisible())
		return;

	// 连续改动（勾选项、切包类型）合并成一次刷新。
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

// 密码只影响加密，不影响打包内容，所以不重建树。
void ExportWidget::passwordChanged() { syncOptions(); }

// 想关掉「每道题单独一个目录」时先看看各题文件会不会撞名。
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

// 勾了「一并导出样例数据」就强制保留内部目录结构。
void ExportWidget::samplesToggled() { optionsChanged(); }

void ExportWidget::syncOptions() {
	builder->setWrapInFolder(wrapBox->isChecked());
	builder->setNestedZip(nestedBox->isChecked());
	builder->setOneFolderPerTask(perTaskBox->isChecked());
	builder->setKeepStructure(structureBox->isChecked());
	builder->setIncludeSamples(samplesBox->isChecked());
	builder->setStatementFile(statementBox->currentText());
	builder->setPassword(encryptBox->isChecked() ? passwordEdit->text() : QString());
}

// 取（必要时逐级创建）目录节点；root 为空表示外层。
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

// 内层压缩包在外层里的节点，比如 answers.zip 里的 day1/HN.zip 要显示在 day1/ 下面。
auto ExportWidget::archiveNode(const QString &path) -> QTreeWidgetItem * {
	const int slash = path.lastIndexOf(QChar('/'));
	const QString parentPath = slash < 0 ? QString() : path.left(slash);
	const QString name = slash < 0 ? path : path.mid(slash + 1);
	QTreeWidgetItem *parent = folderNode(nullptr, parentPath);
	auto *node = parent ? new PackageItem(parent, {name, QString()})
	                    : new PackageItem(contentTree, {name, QString()});
	node->setFolder(false);
	node->setIcon(0, fileIcon);
	return node;
}

// 分批插入：每轮只建一小批节点，建完就返回事件循环，界面始终能响应。
void ExportWidget::buildTreeStep() {
	// 一轮的节点数：图标已经缓存好，一下建几百个也就几毫秒，但又不至于让一帧太长。
	constexpr int perStep = 500;
	int done = 0;

	while (pendingIndex < pendingNodes.size() && done < perStep) {
		const PendingNode &pending = pendingNodes.at(pendingIndex);
		const int slash = pending.path.lastIndexOf(QChar('/'));

		if (pending.folder) {
			// 目录：逐级建出来（建过的会直接复用）。
			folderNode(pending.root, pending.path);
		} else {
			QTreeWidgetItem *parent = folderNode(pending.root, slash < 0 ? QString() : pending.path.left(slash));
			const QString name = slash < 0 ? pending.path : pending.path.mid(slash + 1);
			auto *node = parent ? new PackageItem(parent, {name, QString()})
			                    : new PackageItem(contentTree, {name, QString()});
			node->setFolder(false);
			node->setIcon(0, fileIcon);
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

// 树建完了：写状态栏、决定能不能点导出、排一次序、默认折起来。
void ExportWidget::finishTree() {
	pendingNodes.clear();
	pendingIndex = 0;
	statusLabel->setText(tr("%1 file(s), %2 folder(s)").arg(fileTotal).arg(folderTotal));
	exportButton->setEnabled(fileTotal > 0);

	// 建的时候关着排序（每插一个都重排一次太亏），这里打开就相当于只排一次。
	contentTree->setSortingEnabled(true);

	// 每次刷新（打开比赛日、改选项）都重新建树：默认全部折叠，由用户自己展开。
	contentTree->collapseAll();
}

void ExportWidget::refresh() {
	refreshTimer->stop();
	needsRefresh = false;
	// 上一次的分批建树可能还没建完（比如刚切了包类型）：直接作废。
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

	// 图标取一次就够：style()->standardIcon() 每次要十几毫秒，几万个节点就完蛋了。
	fileIcon = contentTree->style()->standardIcon(QStyle::SP_FileIcon);
	folderIcon = contentTree->style()->standardIcon(QStyle::SP_DirIcon);
	// 建树期间先关掉排序，插完再打开（finishTree()），省得每插一个节点都去排一次。
	contentTree->setSortingEnabled(false);

	syncOptions();
	builder->setContest(curContest);
	// 「各题文件是否重名」要遍历数据，先算好（builder 内部按输入缓存，同一种输入只算一次）。
	duplicatesLocked =
	    builder->kind() == PackageBuilder::TestDataPackage && builder->hasDuplicateTaskFiles();
	// 选项的显示 / 可用状态、内层压缩包名字、说明文字都要跟着包类型走。
	syncOptionWidgets();
	// 重新列一遍 statement/ 下的题面文件（可能在题面选项卡里刚导出过一个新名字）。
	reloadStatementFiles();
	syncOptions();

	const QString dayDirectory = builder->dayRoot();
	builder->setOutputFile(builder->defaultOutputFile());
	outputLabel->setText(QDir::toNativeSeparators(builder->defaultOutputFile()));

	const PackageBuilder::Plan plan = builder->collectItems();

	// 这里只攒「待插入的节点」，不碰控件：真正建节点放到 buildTreeStep() 里分批做。
	for (const PackageBuilder::Item &item : plan.items) {
		PendingNode pending;
		pending.root = nullptr;
		pending.path = item.archivePath;
		pending.source = item.sourcePath;
		pending.folder = item.isDirectory;
		pendingNodes.append(pending);
	}

	// 内层压缩包（可能不止一个，比如每个赛区一个）各自作为一棵子树的根。
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

	if (builder->kind() == PackageBuilder::ContestantPackage &&
	    ! QFileInfo::exists(QDir(dayDirectory).absoluteFilePath(
	        Settings::statementPath() + builder->statementFile())))
		appendLog(tr("The statement file is missing; export it in the Statement tab first if you need it."));
}

void ExportWidget::exportClicked() {
	if (! curContest)
		return;

	syncOptions();

	if (encryptBox->isChecked() && passwordEdit->text().isEmpty()) {
		QMessageBox::warning(this, tr("Export"), tr("Please type a password first."), QMessageBox::Ok);
		return;
	}

	// 每次导出前重新收集一次：这期间可能刚改过题目或补过 down 文件。
	refresh();

	const QString target = builder->defaultOutputFile();
	const QString pdf = QDir(builder->dayRoot()).absoluteFilePath(Settings::statementPath() +
	                                                             builder->statementFile());

	if (builder->kind() == PackageBuilder::ContestantPackage && ! QFileInfo::exists(pdf)) {
		const auto answer = QMessageBox::question(
		    this, tr("Export"),
		    tr("The statement file (%1) does not exist yet, so the package will not contain the statement.\n"
		       "Export anyway?")
		        .arg(QDir::toNativeSeparators(pdf)),
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
