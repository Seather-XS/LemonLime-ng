/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include <QJsonObject>
#include <QList>
#include <QString>

/**
 * 一个比赛日。
 *
 * `file` 是相对于工程根目录的路径，例如 `day1/day1.cdf`。比赛日各自的
 * `data/`、`source/` 都在它自己的子目录里，工作目录在切换比赛日时切过去。
 */
struct DayEntry {
	QString title;
	QString file;
};

/**
 * 三层结构 `比赛(Contest) → 比赛日(Day) → 试题(Task)` 的顶层容器。
 *
 * 工程根目录下的 `contest.conf` 记录比赛标题与比赛日列表；每个比赛日是一份独立的
 * LemonLime 比赛文件（`.cdf`），因此评测、成绩、导出仍然按比赛日各自进行 —— 语义与
 * gengen-tuack 一致。
 */
class DayProject {
  public:
	QString title;
	QList<DayEntry> days;

	/// 判断一个 JSON 对象是不是工程文件（有 `days` 数组）。
	static bool isProjectObject(const QJsonObject &obj);
	static DayProject fromJson(const QJsonObject &obj);
	QJsonObject toJson() const;

	/// 某个比赛日的绝对路径（`root` 为工程根目录）。
	QString dayPath(const QString &root, int index) const;
};
