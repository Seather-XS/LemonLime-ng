/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "admissioncsv.h"
#include "admissionnaming.h"
#include <QDialog>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTableWidget>

class QLabel;
class QPlainTextEdit;
class QUndoStack;
class AdmissionProject;

class FillTable : public QTableWidget {
	Q_OBJECT

  public:
	explicit FillTable(QWidget *parent = nullptr);

  signals:
	void fillRequested(const QRect &source, const QRect &target);

  protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

  private:
	QRect selectionRect() const;
	QRect handleRect() const;

	bool dragging{false};
	QRect source;
	QRect target;
};

class CsvEditorDialog : public QDialog {
	Q_OBJECT

  public:
	explicit CsvEditorDialog(const QString &region, AdmissionTable &table, AdmissionProject *project = nullptr,
	                         QWidget *parent = nullptr);

  private slots:
	void validateClicked();
	void seatsClicked();
	void venuesClicked();
	void idsClicked();
	void headerMenu(const QPoint &position);
	void gridMenu(const QPoint &position);
	void saveClicked();
	void gridEdited();
	void fillRequested(const QRect &source, const QRect &target);
  private:
	void buildUi();
	void retranslate();
	void load();
	void store();
	QStringList rowsAsText() const;
	void fillColumn(int column, int mode);
	void fillSelection(int mode);
	void addColumn();
	void addRowAtEnd();
	void removeColumns(const QList<int> &columns);
	void removeSelectedRows();
	void removeSelectedColumns();
	void dropColumnRules(const QStringList &names);
	void renameColumn(int column);
	void configureColumn(int column);
	void removeColumnRule(int column);
	QStringList headerForValidation() const;
	AdmissionNamingContext sampleContext() const;
	void showColumnStatistics(int column);
	void choosePhotos();
	int photoColumn() const;
	void setPhotoCell(int row, int column, const QString &path);

	QString serialize() const;
	void applyState(const QString &state);
	void pushState(const QString &text);

	QString region;
	AdmissionTable &table;
	AdmissionProject *project{};

	FillTable *grid{};
	QUndoStack *undoStack{};
	QPlainTextEdit *notesEdit{};

	QString lastState;
	bool muted{false};
	bool applyingState{false};
	bool suppressCommandRedo{false};
};
