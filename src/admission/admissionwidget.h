/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

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

class AdmissionWidget : public QWidget {
	Q_OBJECT

  public:
	explicit AdmissionWidget(QWidget *parent = nullptr);
	~AdmissionWidget();
	void changeEvent(QEvent *) override;
	void showEvent(QShowEvent *) override;
	void hideEvent(QHideEvent *) override;

	void setContest(Contest *);
	void setDayContext(const QString &dayFileName, const QString &dayTitle, const QString &contestTitle);
	void refresh();
	int build(const QStringList &regions);
	void saveIfNeeded();

  private slots:
	void refreshClicked();
	void openFolderClicked();
	void editListClicked();
	void newRegionClicked();
	void deleteRegionClicked();
	void tableMenu(const QPoint &position);
	void startSaveTimer();
	void saveTextEdits();
	void notesClicked();
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
	bool loaded{false};

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
	QTimer *saveTimer{};
	QPushButton *newRegionButton{};
	QPushButton *deleteRegionButton{};
	QPushButton *buildSelectedButton{};
	QPushButton *buildAllButton{};
	QPushButton *stopButton{};
	QPushButton *openOutputButton{};
};
