/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QString>
#include <QStringList>
#include <QWidget>

class AdmissionGenerator;
class AdmissionProject;
class Contest;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

/**
 * 「准考证」选项卡：按赛区分组管理名单，并把名单编译成准考证 PDF。
 *
 * 数据在 `admission/`（config.json / contest-notes.md / regions/…），产物在 `dist/admission/`。
 * 生成用的是内置模板（`templates/admission/main.tex`），不提供给用户编辑。
 */
class AdmissionWidget : public QWidget {
	Q_OBJECT

  public:
	explicit AdmissionWidget(QWidget *parent = nullptr);
	~AdmissionWidget();
	void changeEvent(QEvent *) override;
	void showEvent(QShowEvent *) override;
	void hideEvent(QHideEvent *) override;

	void setContest(Contest *);
	/// 与题面选项卡同一套上下文：比赛日文件名、比赛日标题、比赛标题。
	void setDayContext(const QString &dayFileName, const QString &dayTitle, const QString &contestTitle);
	void refresh();
	/// 生成若干赛区（空 = 全部）；期间禁止再次触发，并让界面能响应「停止」。
	int build(const QStringList &regions);
	/// 把输入框里的标题 / 测试时间落盘（关程序、离开选项卡、控件析构时都会调）。
	void saveIfNeeded();

  private slots:
	void refreshClicked();
	void openFolderClicked();
	void editListClicked();
	void newRegionClicked();
	/// 删掉选中赛区的整个目录。
	void deleteRegionClicked();
	/// 赛区表的右键菜单（增删赛区 / 编辑名单 / 注意事项）。
	void tableMenu(const QPoint &position);
	/// 改完标题 / 测试时间存进 config.json。
	void startSaveTimer();
	void saveTextEdits();
	/// 编辑选中赛区的注意事项 / 整场的比赛注意。
	void notesClicked();
	/// 编辑选中赛区的考点 / 考场（含容量）。
	void venuesClicked();
	void contestNotesClicked();
	void buildSelectedClicked();
	void buildAllClicked();
	void stopClicked();
	void openOutputClicked();
	void optionsChanged();

  private:
	void buildUi();
	void retranslate();
	void appendLog(const QString &);
	/// 读盘（第一次会做旧布局迁移）。
	void reload();
	void rebuildTable();
	void updateButtons();
	QString groupAt(int row) const;

	AdmissionProject *project{};
	AdmissionGenerator *generator{};
	Contest *contest{};
	QString dayFileName;
	QString dayTitle;
	QString contestTitle;
	QStringList groups;
	bool building{false};

	QLabel *titleLabel{};
	QLineEdit *titleEdit{};
	QLabel *examTimeLabel{};
	QLineEdit *examTimeEdit{};
	QPushButton *notesButton{};
	QPushButton *contestNotesButton{};
	QTableWidget *table{};
	QComboBox *layoutBox{};
	QComboBox *packageBox{};
	QCheckBox *overwriteBox{};
	QProgressBar *progress{};
	QPlainTextEdit *log{};
	QPushButton *refreshButton{};
	QPushButton *openFolderButton{};
	QPushButton *listButton{};
	QPushButton *venuesButton{};
	/// 标题 / 测试时间的防抖落盘定时器。
	QTimer *saveTimer{};
	QPushButton *newRegionButton{};
	QPushButton *deleteRegionButton{};
	QPushButton *buildSelectedButton{};
	QPushButton *buildAllButton{};
	QPushButton *stopButton{};
	QPushButton *openOutputButton{};
};
