/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "statementeditwidget.h"
#include "base/LemonUtils.hpp"
#include "base/settings.h"
#include "core/contest.h"
#include "core/statementbuilder.h"
#include "findreplacebar.h"
#include "markdownhighlighter.h"
#include "pdfpreview.h"
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextOption>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

#define LEMON_MODULE_NAME "StatementEdit"

namespace {
	const char *defaultStatementMarkdown() {
		return R"MD(---
title: "题面标题"
subtitle: ""
day: ""
date: ""
---

<!-- SECTION: overview_table -->
<!-- END: overview_table -->

<!-- SECTION: submission_filename -->
<!-- END: submission_filename -->

<!-- SECTION: compile_options -->
<!-- END: compile_options -->

<!-- SECTION: notice -->
<!-- END: notice -->

# 题目（problem）

<!-- PROINFO: Standard Input | Standard Output | 1000 ms | 512 MiB -->

## 题目描述

## 输入格式

## 输出格式

## 样例 1 输入

```text

```

## 样例 1 输出

```text

```
)MD";
	}

	const QStringList &defaultProinfoValues() {
		static const QStringList values = {QStringLiteral("Standard Input"), QStringLiteral("Standard Output"),
		                                   QStringLiteral("1000 ms"), QStringLiteral("512 MiB")};
		return values;
	}

	const QMap<QString, QString> &sectionDefaults() {
		static const QMap<QString, QString> defaults = {
		    {QStringLiteral("overview_table"),
		     QStringLiteral("| 题目名称 | A | B | C | D |\n"
		                    "|:-|:-|:-|:-|:-|\n"
		                    "| 题目类型 | 传统型 | 传统型 | 传统型 | 传统型 |\n"
		                    "| 目录 | `a` | `b` | `c` | `d` |\n"
		                    "| 可执行文件名 | `a` | `b` | `c` | `d` |\n"
		                    "| 输入文件名 | `a.in` | `b.in` | `c.in` | `d.in` |\n"
		                    "| 输出文件名 | `a.out` | `b.out` | `c.out` | `d.out` |\n"
		                    "| 每个测试点时限 | 1.0 秒 | 1.0 秒 | 1.0 秒 | 1.0 秒 |\n"
		                    "| 内存限制 | 512 MiB | 512 MiB | 512 MiB | 512 MiB |\n"
		                    "| 测试点数目 | 25 | 25 | 25 | 25 |\n"
		                    "| 测试点是否等分 | 是 | 是 | 是 | 是 |")},
		    {QStringLiteral("submission_filename"),
		     QStringLiteral("| 对于 C++ 语言 | `a.cpp` | `b.cpp` | `c.cpp` | `d.cpp` |\n|:-|:-|:-|:-|:-|")},
		    {QStringLiteral("compile_options"),
		     QStringLiteral("| 对于 C++ 语言 | `-std=c++14 -O2 -static` |\n|:-|:-:|")},
		    {QStringLiteral("notice"),
		     QStringLiteral("**注意事项（请仔细阅读）**\n\n"
		                    "1. 文件名（程序名和输入输出文件名）必须使用英文小写。\n"
		                    "2. `main` 函数的返回值类型必须是 `int`，程序正常结束时的返回值必须是 0。\n"
		                    "3. 提交的程序代码文件的放置位置请参考各省的具体要求。\n"
		                    "4. 因违反以上三点而出现的错误或问题，申诉时一律不予受理。\n"
		                    "5. 若无特殊说明，结果的比较方式为全文比较（过滤行末空格及文末回车）。\n"
		                    "6. 选手提交的程序源文件必须不大于 100KB。\n"
		                    "7. 程序可使用的栈空间内存限制与题目的内存限制一致。")},
		};
		return defaults;
	}

	const QMap<QString, QString> &sectionTitles() {
		static const QMap<QString, QString> titles = {
		    {QStringLiteral("overview_table"), QStringLiteral("题目总览表")},
		    {QStringLiteral("submission_filename"), QStringLiteral("提交源程序文件名")},
		    {QStringLiteral("compile_options"), QStringLiteral("编译选项")},
		    {QStringLiteral("notice"), QStringLiteral("注意事项")},
		};
		return titles;
	}

	QString problemSkeleton() {
		return QStringLiteral("# 题目（problem）\n\n"
		                      "## 题目描述\n\n"
		                      "## 输入格式\n\n"
		                      "## 输出格式\n\n"
		                      "## 样例 1 输入\n\n```text\n\n```\n\n"
		                      "## 样例 1 输出\n\n```text\n\n```\n");
	}

	bool isNoiTemplateName(const QString &name) {
		return name == QStringLiteral("noi") || name == QStringLiteral("noi new");
	}

	QString problemTitleText(const QString &title, const QString &english) {
		const QString name = title.trimmed();
		const QString eng = english.trimmed();

		if (eng.isEmpty())
			return name;

		if (name.isEmpty())
			return eng;

		return QStringLiteral("%1（%2）").arg(name, eng);
	}

	QString problemTitleText(const StatementProblem &problem) {
		return problemTitleText(problem.title, problem.english);
	}
}

QString StatementEditWidget::markdownPath() {
	return Settings::statementPath() + QStringLiteral("statement.md");
}

QString StatementEditWidget::pdfFileName() const {
	return Lemon::common::ResolveStatementPdfName(
	           curContest ? curContest->getStatementPdfName() : QString(), dayFileName, dayTitle,
	           contestTitle) +
	       QStringLiteral(".pdf");
}

QString StatementEditWidget::pdfPath() const { return Settings::statementPath() + pdfFileName(); }

void StatementEditWidget::setDayContext(Contest *contest, const QString &dayFileName,
                                        const QString &dayTitle, const QString &contestTitle) {
	curContest = contest;
	this->dayFileName = dayFileName;
	this->dayTitle = dayTitle;
	this->contestTitle = contestTitle;

	if (pdfNameEdit) {
		QSignalBlocker blocker(pdfNameEdit);
		pdfNameEdit->setText(curContest ? curContest->getStatementPdfName() : QString());
		pdfNameEdit->setPlaceholderText(QStringLiteral("statement"));
	}

	refreshPdfNamePreview();
	emit pdfFileNameChanged(pdfFileName());
}

StatementEditWidget::StatementEditWidget(QWidget *parent) : QWidget(parent) {
	builder = new StatementBuilder();
	buildUi();

	storeTimer = new QTimer(this);
	storeTimer->setSingleShot(true);
	storeTimer->setInterval(400);
	connect(storeTimer, &QTimer::timeout, this, [this] {
		storeEditor();

		if (dirty && saveToDisk())
			setStatus(tr("Saved %1").arg(markdownPath()));
	});

}

void StatementEditWidget::setupPdfPreviewTimer() {
	pdfPreviewTimer = new QTimer(this);
	pdfPreviewTimer->setSingleShot(true);
	pdfPreviewTimer->setInterval(200);
	connect(pdfPreviewTimer, &QTimer::timeout, this, &StatementEditWidget::applyPdfNameToPreview);
}

StatementEditWidget::~StatementEditWidget() {
	if (buildThread) {
		buildThread->wait();
		delete buildThread;
	}

	delete builder;
}

void StatementEditWidget::changeEvent(QEvent *event) {
	if (event->type() == QEvent::LanguageChange) {
		retranslate();
		refreshTree();
		refreshEditor();
	}
}

void StatementEditWidget::buildUi() {
	auto *layout = new QVBoxLayout(this);

	auto *toolBar = new QHBoxLayout();
	toolBar->addWidget(new QLabel(tr("Template:"), this));
	templateBox = new QComboBox(this);
	templateBox->addItems(StatementBuilder::templateNames());
	templateBox->setCurrentText(QStringLiteral("noi new"));
	toolBar->addWidget(templateBox);
	toolBar->addStretch();
	importButton = new QPushButton(tr("Import"), this);
	importButton->setToolTip(tr("Load statement/statement.md into the editor."));
	saveButton = new QPushButton(tr("Save"), this);
	exportButton = new QPushButton(tr("Export and Open PDF"), this);
	exportButton->setToolTip(tr("Compile the statement PDF with pandoc + xelatex, then open it."));
	toolBar->addWidget(importButton);
	toolBar->addWidget(saveButton);
	toolBar->addWidget(exportButton);
	layout->addLayout(toolBar);

	auto *nameBar = new QHBoxLayout();
	nameBar->addWidget(new QLabel(tr("PDF file name:"), this));
	pdfNameEdit = new QLineEdit(this);
	pdfNameEdit->setPlaceholderText(QStringLiteral("statement"));
	pdfNameEdit->setToolTip(tr("File name of the exported PDF (no extension). Placeholders: <day> = contest day file "
	                          "name, <title-day> = contest day title, <title> = contest title."));
	nameBar->addWidget(pdfNameEdit, 1);
	pdfNamePreview = new QLabel(this);
	pdfNamePreview->setStyleSheet(QStringLiteral("color: gray;"));
	pdfNamePreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
	nameBar->addWidget(pdfNamePreview, 1);
	layout->addLayout(nameBar);

	auto *splitter = new QSplitter(Qt::Horizontal, this);
	tree = new QTreeWidget(splitter);
	tree->setHeaderHidden(true);
	tree->setMinimumWidth(180);

	auto *right = new QWidget(splitter);
	auto *rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(0, 0, 0, 0);
	findBar = new FindReplaceBar(right);
	rightLayout->addWidget(findBar);
	stack = new QStackedWidget(right);
	editor = new QPlainTextEdit(stack);
	editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	editor->setTabChangesFocus(false);
	editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	highlighter = new MarkdownHighlighter(editor->document());

	auto *metaPage = new QWidget(stack);
	auto *metaLayout = new QVBoxLayout(metaPage);
	metaLayout->addWidget(new QLabel(tr("Title / subtitle / day / date of the statement."), metaPage));
	auto *form = new QFormLayout();
	const QStringList metaLabels = {tr("Title"), tr("Subtitle"), tr("Day"), tr("Date")};

	for (const QString &label : metaLabels) {
		auto *edit = new QLineEdit(metaPage);
		metaEdits.append(edit);
		form->addRow(label, edit);
	}

	metaLayout->addLayout(form);
	metaLayout->addStretch();
	stack->addWidget(metaPage);
	stack->addWidget(editor);

	problemTitleRow = new QWidget(right);
	auto *titleRowLayout = new QGridLayout(problemTitleRow);
	titleRowLayout->setContentsMargins(0, 0, 0, 0);
	titleRowLayout->addWidget(new QLabel(tr("Problem Title"), problemTitleRow), 0, 0);
	problemTitleEdit = new QLineEdit(problemTitleRow);
	problemTitleEdit->setPlaceholderText(tr("Title only, no leading #"));
	problemTitleEdit->setMaxLength(200);
	titleRowLayout->addWidget(problemTitleEdit, 0, 1);
	titleRowLayout->addWidget(new QLabel(tr("English Name"), problemTitleRow), 0, 2);
	problemEnglishEdit = new QLineEdit(problemTitleRow);
	problemEnglishEdit->setMaxLength(200);
	titleRowLayout->addWidget(problemEnglishEdit, 0, 3);
	titleRowLayout->setColumnStretch(1, 1);
	titleRowLayout->setColumnStretch(3, 1);
	problemTitleRow->hide();

	proinfoRow = new QWidget(right);
	auto *proinfoLayout = new QGridLayout(proinfoRow);
	proinfoLayout->setContentsMargins(0, 0, 0, 0);
	const QStringList proinfoLabels = {tr("Input File"), tr("Output File"), tr("Time Limit"),
	                                   tr("Memory Limit")};
	const QStringList &proinfoHints = defaultProinfoValues();

	for (int i = 0; i < proinfoLabels.size(); i++) {
		auto *edit = new QLineEdit(proinfoRow);
		edit->setPlaceholderText(proinfoHints.at(i));
		edit->setMaxLength(120);
		edit->setToolTip(tr("Filled into \\proinfo of the ccpc template; leave it empty to omit an entry."));
		proinfoEdits.append(edit);
		proinfoLayout->addWidget(new QLabel(proinfoLabels.at(i), proinfoRow), i / 2, (i % 2) * 2);
		proinfoLayout->addWidget(edit, i / 2, (i % 2) * 2 + 1);
	}

	proinfoRow->hide();

	rightLayout->addWidget(problemTitleRow);
	rightLayout->addWidget(proinfoRow);
	rightLayout->addWidget(stack);

	splitter->addWidget(tree);
	splitter->addWidget(right);

	preview = new PdfPreviewWidget(splitter);
	splitter->addWidget(preview);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setStretchFactor(2, 1);
	layout->addWidget(splitter, 1);

	auto *problemBar = new QHBoxLayout();
	auto *addButton = new QToolButton(this);
	addButton->setText(QStringLiteral("＋"));
	addButton->setToolTip(tr("Add a problem"));
	auto *removeButton = new QToolButton(this);
	removeButton->setText(QStringLiteral("－"));
	removeButton->setToolTip(tr("Remove the selected problem"));
	auto *upButton = new QToolButton(this);
	upButton->setText(QStringLiteral("↑"));
	auto *downButton = new QToolButton(this);
	downButton->setText(QStringLiteral("↓"));
	problemBar->addWidget(addButton);
	problemBar->addWidget(removeButton);
	problemBar->addWidget(upButton);
	problemBar->addWidget(downButton);
	problemBar->addStretch();
	statusLabel = new QLabel(this);
	problemBar->addWidget(statusLabel);
	layout->addLayout(problemBar);

	auto *logBox = new QGroupBox(tr("Build log"), this);
	auto *logLayout = new QVBoxLayout(logBox);
	logView = new QPlainTextEdit(logBox);
	logView->setReadOnly(true);
	logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	logView->setMaximumHeight(140);
	logLayout->addWidget(logView);
	layout->addWidget(logBox);

	connect(tree, &QTreeWidget::currentItemChanged, this, [this] { treeSelectionChanged(); });
	connect(editor, &QPlainTextEdit::textChanged, this, &StatementEditWidget::editorTextChanged);
	connect(problemTitleEdit, &QLineEdit::textEdited, this, &StatementEditWidget::problemTitleEdited);
	connect(problemEnglishEdit, &QLineEdit::textEdited, this, &StatementEditWidget::problemTitleEdited);
	connect(problemTitleEdit, &QLineEdit::editingFinished, this,
	        &StatementEditWidget::problemTitleEditFinished);
	connect(problemEnglishEdit, &QLineEdit::editingFinished, this,
	        &StatementEditWidget::problemTitleEditFinished);

	for (auto *edit : metaEdits)
		connect(edit, &QLineEdit::textChanged, this, &StatementEditWidget::metaChanged);

	for (auto *edit : proinfoEdits)
		connect(edit, &QLineEdit::textChanged, this, &StatementEditWidget::proinfoChanged);

	connect(templateBox, &QComboBox::currentTextChanged, this, [this] { templateChanged(); });
	connect(importButton, &QPushButton::clicked, this, &StatementEditWidget::importClicked);
	connect(saveButton, &QPushButton::clicked, this, &StatementEditWidget::saveClicked);
	connect(exportButton, &QPushButton::clicked, this, &StatementEditWidget::exportClicked);
	connect(pdfNameEdit, &QLineEdit::textChanged, this, &StatementEditWidget::pdfNameChanged);
	connect(addButton, &QToolButton::clicked, this, &StatementEditWidget::addProblem);
	connect(removeButton, &QToolButton::clicked, this, &StatementEditWidget::removeProblem);
	connect(upButton, &QToolButton::clicked, this, &StatementEditWidget::moveProblemUp);
	connect(downButton, &QToolButton::clicked, this, &StatementEditWidget::moveProblemDown);
	connect(builder, &StatementBuilder::logMessage, this, &StatementEditWidget::appendLog);

	findBar->setEditor(editor);
	// Bulk replacements run inside an edit block, which QSyntaxHighlighter does not
	// track, so re-run the whole markdown highlighting afterwards.
	connect(findBar, &FindReplaceBar::contentReplaced, this,
	        [this] { highlighter->rehighlight(); });
	const auto findShortcut = new QShortcut(QKeySequence::Find, this);
	connect(findShortcut, &QShortcut::activated, this, [this] { findBar->activate(false); });
	const auto replaceShortcut = new QShortcut(QKeySequence::Replace, this);
	connect(replaceShortcut, &QShortcut::activated, this, [this] { findBar->activate(true); });

	setupPdfPreviewTimer();
	retranslate();
}

void StatementEditWidget::retranslate() {
	if (! pdfNameEdit)
		return;

	importButton->setToolTip(tr("Load statement/statement.md into the editor."));
	exportButton->setText(tr("Export and Open PDF"));
	exportButton->setToolTip(tr("Compile the statement PDF with pandoc + xelatex, then open it."));
	pdfNameEdit->setToolTip(tr("File name of the exported PDF (no extension). Placeholders: <day> = contest day file "
	                           "name, <title-day> = contest day title, <title> = contest title."));
	refreshPdfNamePreview();
}

void StatementEditWidget::loadDocument() {
	const QString path = markdownPath();

	if (! QFileInfo::exists(path)) {
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile file(path);

		if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream stream(&file);
			stream << QString::fromUtf8(defaultStatementMarkdown());
		}
	}

	QFile file(path);
	QString text;

	if (file.open(QIODevice::ReadOnly | QIODevice::Text))
		text = QString::fromUtf8(file.readAll());

	loading = true;
	document = StatementDocument::parse(text, currentProblemHeadingStyle());

	const QList<StatementProblem> problems = document.problems();

	if (! problems.isEmpty())
		document.setProblems(problems);

	editorSlot.clear();

	if (isNoiTemplateName(templateBox->currentText()))
		applyNoiSectionDefaults();

	refreshTree();
	selectSlot(QStringLiteral("meta"));
	setDirty(false);
	loading = false;

	applyPdfNameToPreview();
}

void StatementEditWidget::reload() { loadDocument(); }

bool StatementEditWidget::saveIfNeeded() {
	if (! dirty)
		return true;

	return saveToDisk();
}

void StatementEditWidget::refreshTree() {
	loading = true;
	const QString selected = currentSlot;
	tree->clear();
	metaItem = new QTreeWidgetItem(tree, {tr("Basic Information")});
	metaItem->setData(0, Qt::UserRole, QStringLiteral("meta"));
	sectionGroup = new QTreeWidgetItem(tree, {tr("Sections")});
	problemGroup = new QTreeWidgetItem(tree, {tr("Problems")});

	for (const QString &name : StatementDocument::knownSections()) {
		auto *item = new QTreeWidgetItem(sectionGroup, {sectionTitles().value(name, name)});
		item->setData(0, Qt::UserRole, QStringLiteral("section:") + name);
	}

	const QList<StatementProblem> problems = document.problems();

	for (int i = 0; i < problems.size(); i++) {
		auto *item = new QTreeWidgetItem(problemGroup, {problemLabel(i, problemTitleText(problems.at(i)))});
		item->setData(0, Qt::UserRole, QStringLiteral("problem:") + QString::number(i));
	}

	tree->expandAll();
	applyTemplateVisibility();
	loading = false;
	selectSlot(selected);
}

void StatementEditWidget::selectSlot(const QString &slot) {
	QTreeWidgetItemIterator it(tree);

	while (*it) {
		if ((*it)->data(0, Qt::UserRole).toString() == slot) {
			tree->setCurrentItem(*it);
			return;
		}

		++it;
	}

	if (metaItem)
		tree->setCurrentItem(metaItem);
}

void StatementEditWidget::treeSelectionChanged() {
	if (loading)
		return;

	QTreeWidgetItem *item = tree->currentItem();

	if (! item || item->data(0, Qt::UserRole).toString().isEmpty())
		return;

	storeEditor();
	currentSlot = item->data(0, Qt::UserRole).toString();
	refreshEditor();
}

void StatementEditWidget::refreshEditor() {
	loading = true;
	const bool isMeta = currentSlot == QStringLiteral("meta");
	const bool isProblem = currentSlot.startsWith(QStringLiteral("problem:"));
	problemTitleRow->setVisible(isProblem);
	applyProinfoVisibility();
	stack->setCurrentIndex(isMeta ? 0 : 1);
	editorSlot = currentSlot;

	if (isMeta) {
		const QStringList keys = StatementDocument::editableMetaKeys();

		for (int i = 0; i < metaEdits.size() && i < keys.size(); i++)
			metaEdits[i]->setText(document.metadata(keys.at(i)));
	} else if (currentSlot.startsWith(QStringLiteral("section:"))) {
		editor->setPlainText(document.sectionBody(currentSectionName()));
	} else if (isProblem) {
		const int index = currentSlot.mid(QStringLiteral("problem:").length()).toInt();
		const QList<StatementProblem> problems = document.problems();

		if (index >= 0 && index < problems.size()) {
			const StatementProblem &problem = problems.at(index);
			problemTitleEdit->setText(problem.title);
			problemEnglishEdit->setText(problem.english);

			for (int i = 0; i < proinfoEdits.size(); i++)
				proinfoEdits[i]->setText(problem.proinfo.value(i));

			editor->setPlainText(problem.body.join(QChar('\n')).trimmed());
		} else {
			problemTitleEdit->clear();
			problemEnglishEdit->clear();

			for (auto *edit : proinfoEdits)
				edit->clear();

			editor->clear();
			editorSlot.clear();
			QTimer::singleShot(0, this, [this] {
				if (currentSlot.startsWith(QStringLiteral("problem:")) &&
				    currentSlot.mid(QStringLiteral("problem:").length()).toInt() >=
				        document.problems().size())
					selectSlot(QStringLiteral("meta"));
			});
		}
	}

	loading = false;
}

void StatementEditWidget::scheduleStoreEditor() {
	if (storeTimer)
		storeTimer->start();
}

void StatementEditWidget::storeEditor() {
	if (storeTimer)
		storeTimer->stop();

	if (currentSlot != editorSlot)
		return;

	if (currentSlot == QStringLiteral("meta"))
		return;

	if (currentSlot.startsWith(QStringLiteral("section:"))) {
		const QString name = currentSectionName();
		const QString text = editor->toPlainText();

		if (document.sectionBody(name) == text.trimmed())
			return;

		document.setSectionBody(name, text);
		setDirty(true);
		return;
	}

	if (! currentSlot.startsWith(QStringLiteral("problem:")))
		return;

	const int index = currentSlot.mid(QStringLiteral("problem:").length()).toInt();
	QList<StatementProblem> problems = document.problems();

	if (index < 0 || index >= problems.size())
		return;

	StatementProblem problem;
	problem.title = problemTitleEdit->text().trimmed();
	problem.english = problemEnglishEdit->text().trimmed();
	problem.body = editor->toPlainText().split(QChar('\n'));
	problem.proinfo = currentProinfo();

	const StatementProblem &old = problems.at(index);

	if (old.title == problem.title && old.english == problem.english && old.proinfo == problem.proinfo &&
	    old.body.join(QChar('\n')).trimmed() == problem.body.join(QChar('\n')).trimmed())
		return;

	problems[index] = problem;
	document.setProblems(problems);
	setDirty(true);
}

void StatementEditWidget::refreshProblemLabel() {
	if (! currentSlot.startsWith(QStringLiteral("problem:")))
		return;

	const int index = currentSlot.mid(QStringLiteral("problem:").length()).toInt();
	QTreeWidgetItem *item = problemGroup->child(index);

	if (! item || index < 0)
		return;

	item->setText(0, problemLabel(index, problemTitleText(problemTitleEdit->text(), problemEnglishEdit->text())));
}

QString StatementEditWidget::currentSectionName() const {
	return currentSlot.mid(QStringLiteral("section:").length());
}

QStringList StatementEditWidget::currentProinfo() const {
	QStringList values;

	for (auto *edit : proinfoEdits)
		values.append(edit->text());

	return StatementProblem::normalizedProinfo(values);
}

StatementProblem::HeadingStyle StatementEditWidget::currentProblemHeadingStyle() const {
	if (templateBox && templateBox->currentText() == QStringLiteral("ccpc"))
		return StatementProblem::HeadingStyle::Ccpc;

	return StatementProblem::HeadingStyle::Parenthesized;
}

void StatementEditWidget::applyProblemHeadingStyle() {
	const StatementProblem::HeadingStyle style = currentProblemHeadingStyle();

	if (document.problemHeadingStyle() == style)
		return;

	// Read the problems with the previous style, then rewrite their headings with the new one.
	const QList<StatementProblem> problems = document.problems();
	document.setProblemHeadingStyle(style);

	if (problems.isEmpty())
		return;

	const QString before = document.toMarkdown();
	document.setProblems(problems);

	if (document.toMarkdown() != before)
		setDirty(true);
}

void StatementEditWidget::applyProinfoVisibility() {
	if (! proinfoRow)
		return;

	const bool ccpc = templateBox && templateBox->currentText() == QStringLiteral("ccpc");
	proinfoRow->setVisible(ccpc && currentSlot.startsWith(QStringLiteral("problem:")));
}

void StatementEditWidget::editorTextChanged() {
	if (loading)
		return;

	refreshProblemLabel();
	scheduleStoreEditor();
}

void StatementEditWidget::problemTitleEdited() {
	if (loading || ! currentSlot.startsWith(QStringLiteral("problem:")))
		return;

	storeEditor();
	refreshProblemLabel();
}

void StatementEditWidget::problemTitleEditFinished() {
	if (loading)
		return;

	const QList<QLineEdit *> fields = {problemTitleEdit, problemEnglishEdit};

	for (QLineEdit *edit : fields) {
		QString text = edit->text().trimmed();

		while (text.startsWith(QChar('#')))
			text.remove(0, 1);

		text = text.trimmed();

		if (text != edit->text())
			edit->setText(text);
	}

	problemTitleEdited();
}

void StatementEditWidget::proinfoChanged() {
	if (loading)
		return;

	storeEditor();
}

QString StatementEditWidget::problemLabel(int index, const QString &titleText) const {
	QString text = titleText.trimmed();

	if (text.length() > 14)
		text = text.left(14) + QStringLiteral("…");

	return tr("Problem %1").arg(index + 1) + (text.isEmpty() ? QString() : QStringLiteral("　") + text);
}

void StatementEditWidget::metaChanged() {
	if (loading)
		return;

	const QStringList keys = StatementDocument::editableMetaKeys();

	for (int i = 0; i < metaEdits.size() && i < keys.size(); i++)
		document.setMetadata(keys.at(i), metaEdits.at(i)->text());

	setDirty(true);
}

void StatementEditWidget::applyTemplateVisibility() {
	if (! sectionGroup)
		return;

	const bool ccpc = templateBox->currentText() == QStringLiteral("ccpc");

	for (int i = 0; i < sectionGroup->childCount(); i++)
		sectionGroup->child(i)->setHidden(ccpc);

	applyProinfoVisibility();
}

void StatementEditWidget::applyNoiSectionDefaults() {
	bool anyContent = false;

	for (const QString &name : StatementDocument::knownSections())
		if (! document.sectionBody(name).trimmed().isEmpty())
			anyContent = true;

	if (anyContent)
		return;

	for (auto it = sectionDefaults().constBegin(); it != sectionDefaults().constEnd(); ++it)
		document.setSectionBody(it.key(), it.value());

	setDirty(true);
	setStatus(tr("Filled the default sections for the noi / noi new template."));
}

void StatementEditWidget::templateChanged() {
	if (! sectionGroup)
		return;

	storeEditor();
	applyProblemHeadingStyle();
	applyTemplateVisibility();

	if (isNoiTemplateName(templateBox->currentText()))
		applyNoiSectionDefaults();

	if (currentSlot.startsWith(QStringLiteral("section:")) &&
	    templateBox->currentText() == QStringLiteral("ccpc"))
		selectSlot(QStringLiteral("meta"));
	else
		refreshEditor();
}

void StatementEditWidget::importClicked() {
	if (buildThread || builder->isBuilding()) {
		QMessageBox::information(this, tr("Statement"), tr("A statement build is still running."));
		return;
	}

	loadDocument();
	setStatus(tr("Imported %1").arg(markdownPath()));
}

void StatementEditWidget::saveClicked() {
	storeEditor();

	if (saveToDisk())
		setStatus(tr("Saved %1").arg(markdownPath()));
}

bool StatementEditWidget::saveToDisk() {
	storeEditor();

	QFile file(markdownPath());

	if (! file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QMessageBox::warning(this, tr("Statement"), tr("Cannot write %1").arg(markdownPath()));
		return false;
	}

	{
		QTextStream stream(&file);
		stream << document.toMarkdown();
	}

	setDirty(false);
	return true;
}

void StatementEditWidget::exportClicked() {
	if (buildThread)
		return;

	storeEditor();

	if (! saveToDisk())
		return;

	const QString target = QFileInfo(pdfPath()).absoluteFilePath();
	buildTarget = target;
	logView->clear();
	importButton->setEnabled(false);
	saveButton->setEnabled(false);
	exportButton->setEnabled(false);
	setStatus(tr("Building %1 ...").arg(QFileInfo(target).fileName()));
	preview->setMessage(tr("Building %1 ...").arg(QFileInfo(target).fileName()));

	builder->setTemplate(templateBox->currentText());
	builder->setSourceFile(markdownPath());
	builder->setOutputBase(QFileInfo(target).absolutePath() + QChar('/') + QFileInfo(target).completeBaseName());

	buildOk = false;
	buildThread = QThread::create([this] { buildOk = builder->build(); });
	connect(buildThread, &QThread::finished, this, &StatementEditWidget::statementBuildFinished);
	buildThread->start();
}

void StatementEditWidget::statementBuildFinished() {
	QThread *thread = buildThread;
	buildThread = nullptr;

	if (thread)
		thread->deleteLater();

	importButton->setEnabled(true);
	saveButton->setEnabled(true);
	exportButton->setEnabled(true);

	if (! buildOk) {
		appendLog(tr("Failed: %1").arg(builder->lastError()));
		setStatus(tr("Building %1 failed.").arg(QFileInfo(buildTarget).fileName()));
		preview->setMessage(tr("Building %1 failed.").arg(QFileInfo(buildTarget).fileName()));
		return;
	}

	setStatus(tr("Exported %1").arg(buildTarget));
	preview->setPdf(buildTarget);

	if (! logView->toPlainText().isEmpty())
		appendLog(tr("Done."));

	QDesktopServices::openUrl(QUrl::fromLocalFile(buildTarget));
}

void StatementEditWidget::pdfNameChanged() {
	if (loading || ! curContest)
		return;

	curContest->setStatementPdfName(pdfNameEdit->text());
	refreshPdfNamePreview();
	emit pdfFileNameChanged(pdfFileName());
}

void StatementEditWidget::refreshPdfNamePreview() {
	if (! pdfNamePreview)
		return;

	pdfNamePreview->setText(
	    tr("-> %1").arg(QDir::toNativeSeparators(Settings::statementPath() + pdfFileName())));

	if (pdfPreviewTimer)
		pdfPreviewTimer->start();
	else
		applyPdfNameToPreview();
}

void StatementEditWidget::applyPdfNameToPreview() {
	const QString path = pdfPath();

	if (QFileInfo::exists(path)) {
		preview->setPdf(path);
		return;
	}

	preview->setMessage(tr("Compile the statement to see the preview here."));
	preview->setInfoText(QDir::toNativeSeparators(path));
}

void StatementEditWidget::addProblem() {
	storeEditor();
	QList<StatementProblem> problems = document.problems();
	StatementProblem problem;
	problem.title = tr("New Problem");
	problem.english = QStringLiteral("problem");
	problem.proinfo = defaultProinfoValues();
	problem.body = problemSkeleton().split(QChar('\n')).mid(2);
	problems.append(problem);
	document.setProblems(problems);
	setDirty(true);
	const QString slot = QStringLiteral("problem:") + QString::number(problems.size() - 1);
	editorSlot.clear();
	refreshTree();
	selectSlot(slot);
}

void StatementEditWidget::removeProblem() {
	if (! currentSlot.startsWith(QStringLiteral("problem:")))
		return;

	storeEditor();
	const int index = currentSlot.mid(QStringLiteral("problem:").length()).toInt();
	QList<StatementProblem> problems = document.problems();

	if (index < 0 || index >= problems.size())
		return;

	problems.removeAt(index);
	document.setProblems(problems);
	setDirty(true);
	currentSlot = QStringLiteral("meta");
	editorSlot.clear();
	refreshTree();
}

void StatementEditWidget::moveProblemUp() {
	if (! currentSlot.startsWith(QStringLiteral("problem:")))
		return;

	storeEditor();
	const int index = currentSlot.mid(QStringLiteral("problem:").length()).toInt();
	QList<StatementProblem> problems = document.problems();

	if (index <= 0 || index >= problems.size())
		return;

	problems.swapItemsAt(index, index - 1);
	document.setProblems(problems);
	setDirty(true);
	editorSlot.clear();
	refreshTree();
	selectSlot(QStringLiteral("problem:") + QString::number(index - 1));
}

void StatementEditWidget::moveProblemDown() {
	if (! currentSlot.startsWith(QStringLiteral("problem:")))
		return;

	storeEditor();
	const int index = currentSlot.mid(QStringLiteral("problem:").length()).toInt();
	QList<StatementProblem> problems = document.problems();

	if (index < 0 || index + 1 >= problems.size())
		return;

	problems.swapItemsAt(index, index + 1);
	document.setProblems(problems);
	setDirty(true);
	editorSlot.clear();
	refreshTree();
	selectSlot(QStringLiteral("problem:") + QString::number(index + 1));
}

void StatementEditWidget::setDirty(bool value) {
	dirty = value;

	if (value)
		setStatus(tr("(unsaved changes)"));
}

void StatementEditWidget::appendLog(const QString &line) {
	logView->appendPlainText(line);
}

void StatementEditWidget::setStatus(const QString &text) { statusLabel->setText(text); }
