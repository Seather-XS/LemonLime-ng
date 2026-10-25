/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

class Contest;
class Contestant;

class PackageBuilder : public QObject {
	Q_OBJECT

  public:
	enum Kind { ContestantPackage = 0, TestDataPackage = 1, AnswersPackage = 2 };

	struct Item {
		QString archivePath;
		QString sourcePath;
		bool isDirectory{false};
	};

	struct NestedArchive {
		QString name;
		QList<Item> items;
	};

	struct Plan {
		QList<Item> items;
		QList<NestedArchive> archives;
	};

	explicit PackageBuilder(QObject *parent = nullptr);

	static QStringList kindNames();
	static Kind kindAt(int index);

	void setKind(Kind);
	Kind kind() const { return packageKind; }

	void setDayFile(const QString &);
	void setDayDirectory(const QString &);
	void setContest(Contest *);
	void setOutputFile(const QString &);

	void setWrapInFolder(bool);
	bool wrapInFolder() const { return wrap; }
	void setNestedZip(bool);
	bool nestedZip() const { return nested; }
	void setOneFolderPerTask(bool);
	bool oneFolderPerTask() const { return perTask; }
	void setKeepStructure(bool);
	bool keepStructure() const { return structure; }
	void setIncludeSamples(bool);
	bool includeSamples() const { return samples; }
	void setStatementFiles(const QStringList &);
	QStringList statementFiles() const;

	enum EncryptionMethod {
		Aes256Encryption,
		ZipCryptoEncryption,
	};
	void setEncryptionMethod(EncryptionMethod);
	EncryptionMethod encryptionMethod() const { return encryption; }
	QString innerArchiveName() const;
	bool hasDuplicateTaskFiles();
	void setPassword(const QString &);
	QString password() const { return pass; }

	QString dayName() const;
	QString dayRoot() const;

	QString defaultOutputFile() const;

	Plan collectItems();

	bool build();

	void clearCache();

	QString lastError() const { return error; }

  signals:
	void logMessage(const QString &);

  private:
	bool collectContestantPackage(QList<Item> &items);
	bool collectTestDataPackage(QList<Item> &items);
	bool collectAnswersPackage(Plan &plan);
	QList<QPair<QString, QString>> collectDownFiles(const QString &taskName);
	QList<QPair<QString, QString>> collectTestDataFiles(const class Task *task, bool quiet);
	QList<QPair<QString, QString>> graderFilesForTask(const class Task *task, bool quiet);
	static QString taskConfigKey(const class Task *task);

	Kind packageKind{ContestantPackage};
	QString dayFile;
	QString dayDir;
	QString output;
	Contest *contest{nullptr};
	bool wrap{false};
	bool nested{false};
	bool perTask{true};
	bool structure{true};
	bool samples{false};
	QStringList statements;
	EncryptionMethod encryption{Aes256Encryption};
	QString pass;
	QString error;

	QHash<QString, Plan> planCache;
	QHash<QString, bool> duplicatesCache;
	QHash<QString, QList<QPair<QString, QString>>> taskFileCache;

	QString inputKey() const;
};
