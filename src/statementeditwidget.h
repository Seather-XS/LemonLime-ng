/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "core/statementdocument.h"
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

class StatementEditWidget : public QWidget {
	Q_OBJECT

  public:
	explicit StatementEditWidget(QWidget *parent = nullptr);
	~StatementEditWidget();
	void changeEvent(QEvent *) override;

	static QString markdownPath();
	QString pdfPath() const;
	QString pdfFileName() const;

	void setDayContext(Contest *contest, const QString &dayFileName, const QString &dayTitle,
	                   const QString &contestTitle);
	void reload();
	bool saveIfNeeded();

  signals:
	void pdfFileNameChanged(const QString &fileName);

  private slots:
	void templateChanged();
	void treeSelectionChanged();
	void editorTextChanged();
	void problemTitleEdited();
	void problemTitleEditFinished();
	void proinfoChanged();
	void metaChanged();
	void importClicked();
	void saveClicked();
	void exportClicked();
	void statementBuildFinished();
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
	void applyProblemHeadingStyle();
	StatementProblem::HeadingStyle currentProblemHeadingStyle() const;
	QString currentSectionName() const;
	void refreshProblemLabel();
	QStringList currentProinfo() const;
	void applyProinfoVisibility();
	void refreshPdfNamePreview();
	void applyPdfNameToPreview();
	void retranslate();
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
	QLineEdit *problemEnglishEdit{};
	QWidget *proinfoRow{};
	QList<QLineEdit *> proinfoEdits;
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
	QTimer *pdfPreviewTimer{};
	StatementBuilder *builder{};
	QThread *buildThread{};
	bool buildOk{false};
	QString buildTarget;
	Contest *curContest{};
	QString dayFileName;
	QString dayTitle;
	QString contestTitle;
	QString currentSlot{QStringLiteral("meta")};
	QString editorSlot{QStringLiteral("meta")};
	bool loading{false};
	bool dirty{false};
};
