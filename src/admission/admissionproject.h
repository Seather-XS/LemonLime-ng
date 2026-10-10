/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "admissioncsv.h"
//
#include <QList>
#include <QString>

/** 一个考场：名字 + 容量上限（至少 1 人）。 */
struct AdmissionRoom {
	QString name;
	int capacity{1};
};

/** 一个考点：名字 + 若干考场（考场是考点的元素）。 */
struct AdmissionVenue {
	QString name;
	QList<AdmissionRoom> rooms;
};

/** 一个赛区的全部数据。name 为空表示「未启用赛区」时的那唯一一组。 */
struct AdmissionRegion {
	QString name;
	QString notes;        ///< 注意事项（纯文本 + [文字](链接)）
	AdmissionTable table; ///< contestants.csv（考点 / 考场 / 座位号 / 照片都是选手级的）
	/// 考点 → 考场（含容量）：排座位唯一依据，存 regions/<赛区>/rooms.json。
	QList<AdmissionVenue> venues;

	/// 所有考场的容量加起来。
	int totalCapacity() const;
	/// 考场总数。
	int roomCount() const;
};

/**
 * 准考证的数据模型（比赛日下的 `admission/`）。
 *
 *   config.json         title / examTime / generatePolicy
 *   contest-notes.md    比赛注意（整场一份，纯文本 + [文字](链接)）
 *   regions/            notes.md / contestants.csv / columns.json
 *
 * 测试时间是比赛日级的；考场、额外行等内容都写在名单（contestants.csv）里 ——
 * 锁定行之外的自定义列会按「列名 | 该行单元格」印成额外行。
 * 未启用赛区时 `regions/` 自己就是唯一赛区（不套子目录）；启用后每个赛区一个子目录。
 */
class AdmissionProject {
  public:
	static QString root();
	/// 把根目录钉成绝对路径（打开比赛日时调）：此后读写都不再受进程工作目录影响。
	/// 传空取消钉定（关掉比赛日时调，回到「相对当前工作目录」的老行为）。
	static void useRoot(const QString &path);
	static QString regionsRoot();
	static QString configPath();
	static QString contestNotesPath();
	/// 赛区目录（带结尾分隔符）。
	static QString regionFolder(const QString &region);
	/// 旧数据兼容：老版把测试时间写在赛区的 region.json 里。
	static QString regionJsonPath(const QString &region);
	static QString notesPath(const QString &region);
	static QString contestantsPath(const QString &region);
	static QString columnsPath(const QString &region);
	/// 考点 / 考场（含容量）方案。
	static QString roomsPath(const QString &region);

	QString title;                 ///< 准考证标题；空 → 用比赛日标题
	QString examTime;              ///< 测试时间（比赛日级）
	QString contestNotes;          ///< 比赛注意（纯文本 + [文字](链接)）
	/// 准考证号规则（idRule）：每赛区独立编号、按 CSV 行序。
	QString idTemplate{QStringLiteral("<section>-S<number><number><number><number><number>")};
	QStringList idCharSources;
	/// 输出文件名规则（generatePolicy.naming）：不启用时用 <准考证号>.pdf。
	bool namingEnabled{false};
	QString namingTemplate;
	QStringList namingNumberSources;
	QStringList namingCharSources;
	QString namingReplacement{QStringLiteral("_")};
	int namingMaxLength{64};
	QString dirLayout{"byRegion"}; ///< byRegion / flat / byRoom
	/// 排座位策略（generatePolicy.seat）：考点 / 考场方案是唯一根据，这里只记怎么摊。
	int seatLayout{0};        ///< 0 = 先填满靠前的，1 = 各考场平均
	QString seatOrder{"row"}; ///< row / name / random
	bool packageByRegion{true};
	QString packageName{"admission-{region}"};
	bool overwrite{true};

	QList<AdmissionRegion> regions;

	AdmissionRegion *find(const QString &region);
	const AdmissionRegion *find(const QString &region) const;

	/// 读盘（config.json、contest-notes.md，并扫出赛区）。
	bool load(bool regionEnabled, QString *error);
	/// 写盘（config.json、contest-notes.md，以及每个赛区的 notes.md / contestants.csv / rooms.json）。
	bool save(QString *error) const;
	/// 只写 config.json（标题 / 测试时间 / 策略这种，不动各赛区的名单）。
	bool saveConfig(QString *error) const;

	/// 把考点 / 考场方案写成 JSON（房间容量至少 1）。
	static QString venuesToJson(const QList<AdmissionVenue> &venues);
	/// 从 JSON 读考点 / 考场方案。
	static QList<AdmissionVenue> venuesFromJson(const QString &json);

	/// 新建赛区目录与骨架文件；已经存在返回 false。
	bool createRegion(const QString &region, QString *error);
	/// 删掉整个赛区目录（含名单 / 通告 / 列规则）。
	bool removeRegion(const QString &region, QString *error);
	/// 磁盘上已有的赛区（未启用赛区时是一个空串元素；没有任何赛区时也是空串）。
	static QStringList discoverRegions(bool regionEnabled);
	/// 把旧的扁平布局（admission/<赛区>.csv、<赛区>.md、template.tex）迁到新布局。
	void migrateLegacy(QStringList *log);
};
