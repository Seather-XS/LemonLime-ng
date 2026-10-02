/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QHash>
#include <QIcon>
#include <QList>
#include <QString>
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
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * 「导出」选项卡：把比赛日打包成各平台要的 .zip。
 *
 * 界面只负责选包类型、预览内容、点按钮；打包本身在 core 的 PackageBuilder 里，
 * 与命令行入口共用同一套逻辑。包固定写到 `<比赛日>/dist/export/` 下，不给改路径。
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
	/// 题面选项卡里那个 PDF 名模板算出来的默认文件名（statement/ 下），
	/// 用作下拉框的默认选中项。
	void setDefaultStatementFile(const QString &);
	/// 重新列出打包内容（点「刷新」/ 切到本选项卡时调用）。
	void refresh();

  private slots:
	void kindChanged();
	void optionsChanged();
	void perTaskToggled();
	void samplesToggled();
	void passwordChanged();
	void exportClicked();
	/// 换了题面文件：只需重新列一遍打包内容，不必重扫整个目录。
	void statementChanged();
	/// 预览树分批构建：每触发一次插一小批节点，插完再收尾。
	void buildTreeStep();

  private:
	/// 待插入预览树的一个节点（构建期的临时数据，不含任何控件）。
	struct PendingNode {
		QTreeWidgetItem *root{nullptr}; ///< 所属内层压缩包节点；空表示直接挂在外层
		QString path;                   ///< 包内的完整路径（目录就是目录路径）
		QString source;                 ///< 磁盘上的来源，目录条目为空
		bool folder{false};
	};

	void buildUi();
	void retranslate();
	void appendLog(const QString &);
	/// 把界面上的选项推给 PackageBuilder。
	void syncOptions();
	/// 按当前包类型显示 / 启用选项，并更新说明文字（内部会屏蔽信号，不会递归）。
	void syncOptionWidgets();
	/// 延迟刷新请求：没显示出来就先不算，显示了再合并成一次刷新。
	void scheduleRefresh();
	/// 取（必要时逐级创建）某棵树下某个目录路径对应的节点。
	QTreeWidgetItem *folderNode(QTreeWidgetItem *root, const QString &path);
	/// 内层压缩包在外层里的节点（路径可能带目录，如 day1/HN.zip）。
	QTreeWidgetItem *archiveNode(const QString &path);
	/// 收尾：写状态栏、决定导出按钮能不能点、把树折起来。
	void finishTree();
	/// 重新列出 `statement/` 下的文件，选中当前 / 默认题面。
	void reloadStatementFiles();
	/// 下拉框当前项对应的真文件名（去掉「 (missing)」装饰）。
	QString selectedStatementFile() const;

	Contest *curContest{nullptr};
	PackageBuilder *builder{nullptr};
	QString dayFile;
	/// 默认题面文件（由题面选项卡的模板算出来）。
	QString defaultStatementFile;
	/// 刷新合并用：连续改选项 / 敲密码时只重建一次树。
	QTimer *refreshTimer{nullptr};
	bool needsRefresh{false};
	/// 各题文件重名（重名时「每道题单独一个目录」不能取消），只在刷新时算一次。
	bool duplicatesLocked{false};
	QComboBox *kindBox{nullptr};
	QLabel *typeLabel{nullptr};
	QLabel *outputTitleLabel{nullptr};
	QLabel *outputLabel{nullptr};
	QGroupBox *optionsBox{nullptr};
	QCheckBox *wrapBox{nullptr};
	QCheckBox *nestedBox{nullptr};
	QCheckBox *perTaskBox{nullptr};
	QCheckBox *structureBox{nullptr};
	QCheckBox *samplesBox{nullptr};
	QComboBox *statementBox{nullptr};
	QLabel *statementLabel{nullptr};
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

	// ---- 预览树的增量构建 ----
	/// 分批插节点的定时器（0ms，让出一帧再继续）。
	QTimer *treeTimer{nullptr};
	QList<PendingNode> pendingNodes;
	int pendingIndex{0};
	/// 目录路径 → 节点。外层与每个内层包各有一套，所以按根节点分开存。
	QHash<QTreeWidgetItem *, QHash<QString, QTreeWidgetItem *>> folderNodes;
	/// 图标只取一次：style()->standardIcon() 每次要十几毫秒，每个节点都调会卡死。
	QIcon fileIcon;
	QIcon folderIcon;
	int fileTotal{0};
	int folderTotal{0};
};
