/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "core/statementdocument.h"
//
#include <QList>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class PdfPreviewWidget;
class MarkdownHighlighter;
class FindReplaceBar;
class StatementBuilder;
class QThread;
class Contest;

/**
 * 「题面」选项卡。
 *
 * 每个比赛日一个 `statement/` 目录：读写的 markdown 固定是 `statement/statement.md`，
 * 导出的 PDF 名字可以由用户在界面上写模板（不带扩展名，支持 `<day>` / `<title-day>` /
 * `<title>` 占位符），默认是 `statement/statement.pdf`；目录不可更改。
 * 编辑方式与 statement-editor 相同：左树上是基础信息、四个区段与各道试题。
 */
class StatementEditWidget : public QWidget {
	Q_OBJECT

  public:
	explicit StatementEditWidget(QWidget *parent = nullptr);
	~StatementEditWidget();
	void changeEvent(QEvent *) override;

	/// 相对比赛日目录的固定路径。
	static QString markdownPath();
	/// 导出的 PDF：路径由 Contest::getStatementPdfName() 的模板决定。
	QString pdfPath() const;
	/// 模板里占位符都替完之后的纯文件名（不带目录、带 .pdf 后缀）。
	QString pdfFileName() const;

	/// 比赛日上下文：比赛日文件名 / 比赛日标题 / 比赛标题（占位符用）。
	void setDayContext(Contest *contest, const QString &dayFileName, const QString &dayTitle,
	                   const QString &contestTitle);
	/// 比赛日切换后重新载入（会先把当前内容写回 statement.md）。
	void reload();
	/// 有未保存的改动时写回磁盘；返回是否成功。
	bool saveIfNeeded();

  signals:
	/// 导出的 PDF 文件名（已替完占位符）发生变化。
	void pdfFileNameChanged(const QString &fileName);

  private slots:
	void templateChanged();
	void treeSelectionChanged();
	void editorTextChanged();
	void problemTitleEdited();
	void problemTitleEditFinished();
	void metaChanged();
	void importClicked();
	void saveClicked();
	/// 编译并打开 PDF（工具条上只有一个按钮）。
	void exportClicked();
	/// 后台编译结束：恢复按钮、刷新日志与预览（在编译线程发 finished 后于主线程调用）。
	void statementBuildFinished();
	/// 模板改了：存进比赛日，并刷新输入框旁边的预览名字。
	void pdfNameChanged();
	void addProblem();
	void removeProblem();
	void moveProblemUp();
	void moveProblemDown();

  private:
	void buildUi();
	void loadDocument();
	void refreshTree();
	void refreshEditor();
	void storeEditor();
	void scheduleStoreEditor();
	void selectSlot(const QString &slot);
	void applyTemplateVisibility();
	void applyNoiSectionDefaults();
	QString currentSectionName() const;
	void refreshProblemLabel();
	/// 刷新「名字会变成 …」预览与预览窗的提示文字。
	void refreshPdfNamePreview();
	/// 把当前文件名对应的 PDF 交给预览窗（不存在就写清楚期望的是哪个文件）。
	void applyPdfNameToPreview();
	/// 切语言时把按钮 / 提示文字重设一遍。
	void retranslate();
	/// 建「改文件名后换预览」的去抖定时器（buildUi() 结束时调）。
	void setupPdfPreviewTimer();
	QString problemLabel(int index, const QString &titleText) const;
	void setDirty(bool value);
	bool saveToDisk();
	void appendLog(const QString &line);
	void setStatus(const QString &text);

	StatementDocument document;
	QTreeWidget *tree{};
	QTreeWidgetItem *metaItem{};
	QTreeWidgetItem *sectionGroup{};
	QTreeWidgetItem *problemGroup{};
	QStackedWidget *stack{};
	QPlainTextEdit *editor{};
	QWidget *problemTitleRow{};
	QLineEdit *problemTitleEdit{};
	QLineEdit *pdfNameEdit{};
	QLabel *pdfNamePreview{};
	QList<QLineEdit *> metaEdits;
	QComboBox *templateBox{};
	QPushButton *importButton{};
	QPushButton *saveButton{};
	QPushButton *exportButton{};
	QLabel *statusLabel{};
	QPlainTextEdit *logView{};
	PdfPreviewWidget *preview{};
	MarkdownHighlighter *highlighter{};
	FindReplaceBar *findBar{};
	QTimer *storeTimer{};
	/// 改文件名时预览要读盘（甚至起 pdftocairo），敲字期间攒一下再换。
	QTimer *pdfPreviewTimer{};
	StatementBuilder *builder{};
	/// 题面 PDF 在后台线程里编译，xelatex 期间不阻塞界面。
	QThread *buildThread{};
	bool buildOk{false};
	QString buildTarget;
	Contest *curContest{};
	QString dayFileName;
	QString dayTitle;
	QString contestTitle;
	QString currentSlot{QStringLiteral("meta")};
	bool loading{false};
	bool dirty{false};
};
