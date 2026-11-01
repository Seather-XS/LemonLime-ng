/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "core/datagenerator.h"
#include <QString>
#include <QWidget>

class Contest;
class Settings;
class Task;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QThread;
class QTimer;

class DataWidget : public QWidget {
	Q_OBJECT

  public:
	explicit DataWidget(QWidget *parent = nullptr);
	~DataWidget();
	void changeEvent(QEvent *) override;
	void showEvent(QShowEvent *) override;

	void setContest(Contest *);
	void setSettings(Settings *);
	void refresh();

  signals:
	void contestModified();
	void statusChanged(const QString &message);

  private slots:
	void taskSelectionChanged();
	void generateClicked();
	void openGeneratorFolder();
	void openGeneratorFile();
	void openSolutionFile();
	void generateFinished();
	void saveConfig();
	void fileStarted(int testIndex, int stage);
	void fileFinished(int testIndex, int stage, bool ok, const QString &detail);
	void regenerateSelected();
	void updateRegenerateState();
	void showResultMenu(const QPoint &pos);

  private:
	void buildUi();
	void retranslate();
	void appendLog(const QString &line);
	void setStatus(const QString &text);
	void updateParameterState();
	void rebuildResultTable(bool force = false);
	void refreshRowLabel(int planIndex);
	void setRowStatus(int fileRow, int status);
	int planIndexAt(const QModelIndex &index) const;
	void applyStoredStatuses();
	void saveResultStatuses();
	void resetRowStates();
	void resetPlanStates(const QList<int> &indices);
	void regenerateTests(const QList<int> &indices);
	QList<int> selectedPlanIndices() const;
	QStringList columnLabels() const;
	void openSourceFile(const QString &baseName);
	DataGenerator::Options uiOptions() const;
	void loadConfigFor(Task *task);
	void flushConfig();
	void scheduleSaveConfig();
	Task *currentTask() const;
	Task *taskForDirectory(const QString &directory) const;
	bool collectOptions(Task *task, DataGenerator::Options *options, QString *error);

	Contest *curContest{};
	Settings *settings{};
	DataGenerator *generator{};

	QListWidget *taskList{};
	QLineEdit *patternEdit{};
	QLineEdit *samplePatternEdit{};
	QCheckBox *subtaskBox{};
	QSpinBox *caseSpin{};
	QLabel *subtaskLabel{};
	QLineEdit *subtaskEdit{};
	QCheckBox *subtaskNamingBox{};
	QSpinBox *sampleSpin{};
	QLineEdit *inputSuffixEdit{};
	QLineEdit *outputSuffixEdit{};
	QLineEdit *sampleSuffixEdit{};
	QCheckBox *overwriteBox{};
	QCheckBox *appendBox{};
	QPushButton *generateButton{};
	QPushButton *openFolderButton{};
	QPushButton *editGeneratorButton{};
	QPushButton *editSolutionButton{};
	QPushButton *regenerateButton{};
	QLabel *statusLabel{};
	QTableWidget *resultTable{};

	QList<DataGenerator::PlannedTest> plan;
	QList<int> rowStates;
	QList<QStringList> rowDetails;
	QList<QString> rowPaths;
	QList<QLabel *> planStatusLabels;
	QList<QList<int>> rowPlans;
	QList<int> planToRow;
	int currentDetailRow{-1};
	QString planSignature;

	QThread *generateThread{};
	bool generateOk{false};
	bool pendingPartial{false};
	QList<int> pendingIndices;
	QString pendingTaskDirectory;
	QString configTask;
	QTimer *configTimer{};
	bool loadingConfig{false};
	DataGenerator::Options pendingOptions;
	DataGenerator::Result pendingResult;
};
