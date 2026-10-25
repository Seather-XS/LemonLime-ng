/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QHash>
#include <QIcon>
#include <QList>
#include <QString>
#include <QWidget>

#include <memory>

class Contest;
class PackageBuilder;
class QCheckBox;
class QComboBox;
class QFileIconProvider;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

class ExportWidget : public QWidget {
	Q_OBJECT

  public:
	explicit ExportWidget(QWidget *parent = nullptr);
	~ExportWidget();
	void changeEvent(QEvent *) override;
	void showEvent(QShowEvent *) override;

	void setContest(Contest *);
	void setDayFile(const QString &);
	void setDefaultStatementFile(const QString &);
	void refresh();

  private slots:
	void kindChanged();
	void optionsChanged();
	void perTaskToggled();
	void samplesToggled();
	void passwordChanged();
	void exportClicked();
	void statementChanged();
	void buildTreeStep();

  private:
	struct PendingNode {
		QTreeWidgetItem *root{nullptr};
		QString path;
		QString source;
		bool folder{false};
	};

	void buildUi();
	void retranslate();
	void appendLog(const QString &);
	void syncOptions();
	void syncOptionWidgets();
	void scheduleRefresh();
	QTreeWidgetItem *folderNode(QTreeWidgetItem *root, const QString &path);
	QTreeWidgetItem *archiveNode(const QString &path);
	QIcon fileIconFor(const QString &fileName);
	void finishTree();
	void reloadStatementFiles();
	QStringList checkedStatementFiles() const;
	QStringList missingStatementFiles() const;

	Contest *curContest{nullptr};
	PackageBuilder *builder{nullptr};
	QString dayFile;
	QString defaultStatementFile;
	QTimer *refreshTimer{nullptr};
	bool needsRefresh{false};
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
	QListWidget *statementList{nullptr};
	QPushButton *statementAllButton{nullptr};
	QPushButton *statementNoneButton{nullptr};
	QLabel *statementLabel{nullptr};
	bool statementSelectionCustomized{false};
	bool statementSelectionInitialized{false};
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

	QTimer *treeTimer{nullptr};
	QList<PendingNode> pendingNodes;
	int pendingIndex{0};
	QHash<QTreeWidgetItem *, QHash<QString, QTreeWidgetItem *>> folderNodes;
	std::unique_ptr<QFileIconProvider> iconProvider;
	QHash<QString, QIcon> fileTypeIcons;
	QIcon unknownFileIcon;
	QIcon fallbackFileIcon;
	QIcon folderIcon;
	int fileTotal{0};
	int folderTotal{0};
};
