/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QList>
#include <QWidget>

class Contest;
class PackageBuilder;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

/**
 * 「导出」选项卡：把比赛日打包成各平台要的 .zip。
 *
 * 界面只负责选包类型、预览内容、点按钮；打包本身在 core 的 PackageBuilder 里，
 * 与命令行入口共用同一套逻辑。包固定写到 `<比赛日>/export/` 下，不给改路径。
 */
class ExportWidget : public QWidget {
	Q_OBJECT

  public:
	explicit ExportWidget(QWidget *parent = nullptr);
	~ExportWidget();
	void changeEvent(QEvent *) override;
	void showEvent(QShowEvent *) override;

	void setContest(Contest *);
	/// 比赛日文件（.cdf）：用来命名套层目录与默认 zip。
	void setDayFile(const QString &);
	/// 重新列出打包内容（点「刷新」/ 切到本选项卡时调用）。
	void refresh();

  private slots:
	void kindChanged();
	void optionsChanged();
	void perTaskToggled();
	void samplesToggled();
	void passwordChanged();
	void exportClicked();

  private:
	void buildUi();
	void retranslate();
	void appendLog(const QString &);
	/// 把界面上的选项推给 PackageBuilder。
	void syncOptions();
	/// 按当前包类型显示 / 启用选项，并更新说明文字（内部会屏蔽信号，不会递归）。
	void syncOptionWidgets();
	/// 延迟刷新请求：没显示出来就先不算，显示了再合并成一次刷新。
	void scheduleRefresh();

	Contest *curContest{nullptr};
	PackageBuilder *builder{nullptr};
	QString dayFile;
	/// 刷新合并用：连续改选项 / 敲密码时只重建一次树。
	QTimer *refreshTimer{nullptr};
	bool needsRefresh{false};
	/// 各题文件重名（重名时「每道题单独一个目录」不能取消），只在刷新时算一次。
	bool duplicatesLocked{false};
	QComboBox *kindBox{nullptr};
	QLabel *typeLabel{nullptr};
	QLabel *hintLabel{nullptr};
	QLabel *outputTitleLabel{nullptr};
	QLabel *outputLabel{nullptr};
	QGroupBox *optionsBox{nullptr};
	QCheckBox *wrapBox{nullptr};
	QCheckBox *nestedBox{nullptr};
	QCheckBox *perTaskBox{nullptr};
	QCheckBox *structureBox{nullptr};
	QCheckBox *samplesBox{nullptr};
	QCheckBox *encryptBox{nullptr};
	QCheckBox *showPasswordBox{nullptr};
	QLineEdit *passwordEdit{nullptr};
	QGroupBox *contentsBox{nullptr};
	QTreeWidget *contentTree{nullptr};
	QPushButton *refreshButton{nullptr};
	QPushButton *exportButton{nullptr};
	QLabel *statusLabel{nullptr};
	QGroupBox *logBox{nullptr};
	QPlainTextEdit *logView{nullptr};
};
