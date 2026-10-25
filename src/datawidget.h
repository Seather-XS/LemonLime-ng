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
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
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

  private:
	void buildUi();
	void retranslate();
	void appendLog(const QString &line);
	void setStatus(const QString &text);
	void updateParameterState();
	void openSourceFile(const QString &baseName);
	DataGenerator::Options uiOptions() const;
	void loadConfigFor(Task *task);
	void flushConfig();
	void scheduleSaveConfig();
	Task *currentTask() const;
	bool collectOptions(Task *task, DataGenerator::Options *options, QString *error);

	Contest *curContest{};
	Settings *settings{};
	DataGenerator *generator{};

	QListWidget *taskList{};
	QLineEdit *patternEdit{};
	QCheckBox *subtaskBox{};
	QSpinBox *caseSpin{};
	QLabel *subtaskLabel{};
	QLineEdit *subtaskEdit{};
	QCheckBox *subtaskNamingBox{};
	QSpinBox *sampleSpin{};
	QLineEdit *inputSuffixEdit{};
	QLineEdit *outputSuffixEdit{};
	QLineEdit *sampleSuffixEdit{};
	QCheckBox *cleanBox{};
	QCheckBox *configureBox{};
	QPushButton *generateButton{};
	QPushButton *openFolderButton{};
	QPushButton *editGeneratorButton{};
	QPushButton *editSolutionButton{};
	QLabel *previewLabel{};
	QLabel *statusLabel{};
	QPlainTextEdit *logView{};

	QThread *generateThread{};
	bool generateOk{false};
	QString configTask;
	QTimer *configTimer{};
	bool loadingConfig{false};
	DataGenerator::Options pendingOptions;
	DataGenerator::Result pendingResult;
};
