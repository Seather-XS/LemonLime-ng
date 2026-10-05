/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "admissioncsv.h"
#include "admissionnaming.h"
//
#include <QDialog>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTableWidget>

class QLabel;
class QPlainTextEdit;
class QUndoStack;
class AdmissionProject;

/**
 * 带填充柄的表格：选区右下角的小方块可以拖出「往下 / 往右」的填充。
 *
 * 数字列 / 数字行按递增填，其余按块循环复制；拖完发 fillRequested，由对话框去改数据。
 */
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
	/// 当前选区（单元格坐标：x = 列，y = 行）。
	QRect selectionRect() const;
	/// 填充柄的位置（视口坐标）。
	QRect handleRect() const;

	bool dragging{false};
	QRect source;
	QRect target;
};

/**
 * 名单编辑器：固定 5 个内置列 + 任意自定义列。
 *
 * 右键列头：整列填入（固定值 / 序列 / 按行号 / 清空）、加列 / 改名 / 删列、列生成规则（模板或随机）、列统计。
 * 框选一片单元格后右键：用固定值或序列填充选区；也可以直接拖选区右下角的填充柄。
 * Ctrl+Z / Ctrl+Y 撤销重做（按快照，一步一张表）。
 */
class CsvEditorDialog : public QDialog {
	Q_OBJECT

  public:
	explicit CsvEditorDialog(const QString &region, AdmissionTable &table, AdmissionProject *project = nullptr,
	                         QWidget *parent = nullptr);

  private slots:
	void validateClicked();
	void seatsClicked();
	/// 打开本赛区的考点 / 考场编辑区。
	void venuesClicked();
	void idsClicked();
	void headerMenu(const QPoint &position);
	void gridMenu(const QPoint &position);
	void saveClicked();
	/// 用户直接在格子里敲了东西。
	void gridEdited();
	/// 拖完填充柄之后。
	void fillRequested(const QRect &source, const QRect &target);
  private:
	void buildUi();
	void retranslate();
	void load();
	void store();
	QStringList rowsAsText() const;
	/// 整列 / 选区的几种填入方式。
	void fillColumn(int column, int mode);
	void fillSelection(int mode);
	void addColumn();
	/// 在名单末尾添一行空行。
	void addRowAtEnd();
	/// 删掉这几列（内置列跳过），并清掉它们的生成规则。
	void removeColumns(const QList<int> &columns);
	void removeSelectedRows();
	void removeSelectedColumns();
	/// 删掉这些列名对应的生成规则。
	void dropColumnRules(const QStringList &names);
	void renameColumn(int column);
	/// 给某一列配生成规则（模板或随机，写到该赛区的 columns.json）。
	void configureColumn(int column);
	/// 删掉某一列的生成规则。
	void removeColumnRule(int column);
	/// 当前表头（校验模板用）。
	QStringList headerForValidation() const;
	/// 拿第一行的值凑一个预览上下文。
	AdmissionNamingContext sampleContext() const;
	/// 某一列的非空 / 去重 / 长度统计。
	void showColumnStatistics(int column);
	/// 给「照片」列选图片（写相对赛区目录的路径）。
	void choosePhotos();
	/// 照片列的列号（没有就是 -1）。
	int photoColumn() const;
	/// 往格子里写照片路径 + 缩略图。
	void setPhotoCell(int row, int column, const QString &path);

	/// 把整张表序列化成文本（表头一行 + 每行一行，制表符分隔）。
	QString serialize() const;
	/// 反序列化并刷新界面。
	void applyState(const QString &state);
	/// 界面改完之后记一步撤销；内容和上次一样就不记。
	void pushState(const QString &text);

	QString region;
	AdmissionTable &table;
	AdmissionProject *project{};

	FillTable *grid{};
	QUndoStack *undoStack{};
	/// 下半部分：本赛区的「注意事项」（纯文本 + [文字](链接)）。
	QPlainTextEdit *notesEdit{};

	QString lastState;
	/// 正在批量改 / 正在刷新界面：不要记撤销。
	bool muted{false};
	/// 正在应用撤销 / 重做里那一步快照：期间绝不能往撤销栈里 push（会把栈搞乱）。
	bool applyingState{false};
	/// push 命令时不要立刻把新状态再应用一遍。
	bool suppressCommandRedo{false};
};
