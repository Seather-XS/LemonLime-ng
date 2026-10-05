/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QList>
#include <QString>
#include <QStringList>

/**
 * 名单：6 个内置列（顺序固定，不可删改）+ 任意自定义列。
 * 所有内置列都是选手级的值，留空就是空（没有赛区级 / 场次级默认值）。
 * 自定义列不参与锁定行，生成时会按「列名 | 该行单元格」印成额外行。
 */
struct AdmissionTable {
	QStringList header;
	QList<QStringList> rows;

	/// 内置列：姓名 / 准考证号 / 考点 / 考场 / 座位号 / 照片（前端固定、不可删改名）。
	static QStringList builtinColumns();

	/// 表头为空时补成内置 5 列；每行补齐 / 截断到表头长度。
	void normalize();
	int columnIndex(const QString &name) const;
	QString cell(int row, int column) const;

	/// 生成时可以直接用的值（行不足就补空行）。
	QStringList rowAt(int index) const;
};

namespace AdmissionCsv {
	/// 极简 RFC 4180：双引号包裹、引号内逗号 / 换行、"" 转义；空行与 # 开头的行跳过。
	bool parse(const QString &text, AdmissionTable &table, QString *error);
	QString write(const AdmissionTable &table);
	/// 带 BOM 写盘（Excel 直接打得开）。
	bool readFile(const QString &path, AdmissionTable &table, QString *error);
	bool writeFile(const QString &path, const AdmissionTable &table, QString *error);
	/// 只写表头（「导出空模板」用）。
	bool writeTemplate(const QString &path, const QStringList &header, QString *error);
	QString escape(const QString &value);
	bool looksLikeGbk(const QByteArray &raw);
} // namespace AdmissionCsv
