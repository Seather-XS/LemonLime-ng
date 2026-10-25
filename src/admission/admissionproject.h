/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "admissioncsv.h"
#include <QList>
#include <QString>

struct AdmissionRoom {
	QString name;
	int capacity{1};
};

struct AdmissionVenue {
	QString name;
	QList<AdmissionRoom> rooms;
};

struct AdmissionRegion {
	QString name;
	QString notes;
	AdmissionTable table;
	QList<AdmissionVenue> venues;

	int totalCapacity() const;
	int roomCount() const;
};

class AdmissionProject {
  public:
	static QString root();
	static void useRoot(const QString &path);
	static QString regionsRoot();
	static QString configPath();
	static QString contestNotesPath();
	static QString regionFolder(const QString &region);
	static QString regionJsonPath(const QString &region);
	static QString notesPath(const QString &region);
	static QString contestantsPath(const QString &region);
	static QString columnsPath(const QString &region);
	static QString roomsPath(const QString &region);

	QString title;
	QString examTime;
	QString contestNotes;
	QString idTemplate{QStringLiteral("<section>-S<number><number><number><number><number>")};
	QStringList idCharSources;
	bool namingEnabled{false};
	QString namingTemplate;
	QStringList namingNumberSources;
	QStringList namingCharSources;
	QString namingReplacement{QStringLiteral("_")};
	int namingMaxLength{64};
	QString dirLayout{"byRegion"};
	int seatLayout{0};
	QString seatOrder{"row"};
	bool packageByRegion{true};
	QString packageName{"admission-{region}"};
	bool overwrite{true};

	QList<AdmissionRegion> regions;

	AdmissionRegion *find(const QString &region);
	const AdmissionRegion *find(const QString &region) const;

	bool load(bool regionEnabled, QString *error);
	bool save(QString *error) const;
	bool saveConfig(QString *error) const;

	static QString venuesToJson(const QList<AdmissionVenue> &venues);
	static QList<AdmissionVenue> venuesFromJson(const QString &json);

	bool createRegion(const QString &region, QString *error);
	bool removeRegion(const QString &region, QString *error);
	static QStringList discoverRegions(bool regionEnabled);
	void migrateLegacy(QStringList *log);
};
