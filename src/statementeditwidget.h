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

/**
 * 「题面」选项卡。
 *
 * 每个比赛日一个 `statement/` 目录：读写的 markdown 固定是 `statement/statement.md`，
 * 导出的 PDF 固定是 `statement/statement.pdf`（不额外建 dist/ 之类目录），
 * 目录不可更改。编辑方式与 statement-editor 相同：左树上是基础信息、四个区段与各道试题。
 */
class StatementEditWidget : public QWidget {
	Q_OBJECT

  public:
	explicit StatementEditWidget(QWidget *parent = nullptr);
	~StatementEditWidget();
	void changeEvent(QEvent *) override;

	/// 相对比赛日目录的固定路径。
	static QString markdownPath();
	static QString pdfPath();

	/// 比赛日切换后重新载入（会先把当前内容写回 statement.md）。
	void reload();
	/// 有未保存的改动时写回磁盘；返回是否成功。
	bool saveIfNeeded();

  private slots:
	void templateChanged();
	void treeSelectionChanged();
	void editorTextChanged();
	void problemTitleEdited();
	void problemTitleEditFinished();
	void metaChanged();
	void importClicked();
	void saveClicked();
	void exportClicked();
	void openPdfClicked();
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
	QList<QLineEdit *> metaEdits;
	QComboBox *templateBox{};
	QPushButton *importButton{};
	QPushButton *saveButton{};
	QPushButton *exportButton{};
	QPushButton *openPdfButton{};
	QLabel *statusLabel{};
	QPlainTextEdit *logView{};
	PdfPreviewWidget *preview{};
	MarkdownHighlighter *highlighter{};
	FindReplaceBar *findBar{};
	QTimer *storeTimer{};
	StatementBuilder *builder{};
	QString currentSlot{QStringLiteral("meta")};
	bool loading{false};
	bool dirty{false};
};
