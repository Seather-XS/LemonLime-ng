/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

class StatementBuilder : public QObject {
	Q_OBJECT

  public:
	explicit StatementBuilder(QObject *parent = nullptr);

	static QStringList templateNames();
	static QString templateRoot();
	static QString toolsReport();

	void setTemplate(const QString &name);
	void setSourceFile(const QString &path);
	void setOutputBase(const QString &pathWithoutSuffix);

	bool build();
	bool isBuilding() const { return building.load(); }

	QString lastError() const { return errorText; }

  signals:
	void logMessage(const QString &message);

  private:
	void log(const QString &message);
	void fail(const QString &message);

	QString templateName;
	QString sourceFile;
	QString outputBase;
	QString errorText;
	std::atomic<bool> building{false};
};
