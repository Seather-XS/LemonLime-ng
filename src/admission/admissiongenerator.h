/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class AdmissionProject;

class AdmissionGenerator : public QObject {
	Q_OBJECT

  public:
	explicit AdmissionGenerator(QObject *parent = nullptr);

	void cancel() { cancelled = true; }
	bool isCancelled() const { return cancelled; }

	int generate(AdmissionProject &project, const QStringList &onlyRegions, const QString &dayTitle,
	             QString *error);

	static QString outputRoot();
	static QString toolsReport();

  signals:
	void logMessage(const QString &message);
	void progress(int done, int total);

  private:
	bool cancelled{false};
};
