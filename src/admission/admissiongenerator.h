/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QObject>
#include <QString>
#include <QStringList>

class AdmissionProject;

/**
 * 准考证生成器：名单里一行 = 一份 PDF（xelatex 编译两次）。
 *
 * 输出固定写到 `<比赛日>/dist/admission/` 下，可选按赛区 / 按考场分子目录、按赛区打包 zip。
 * 生成前会（在勾了「覆盖旧产物」时）清空输出目录；编译在临时沙箱里进行，失败的会留下 .tex / .log。
 */
class AdmissionGenerator : public QObject {
	Q_OBJECT

  public:
	explicit AdmissionGenerator(QObject *parent = nullptr);

	void cancel() { cancelled = true; }
	bool isCancelled() const { return cancelled; }

	/// 生成若干赛区（onlyRegions 为空 = 全部）。返回失败的张数；-1 表示准备阶段就失败了。
	int generate(AdmissionProject &project, const QStringList &onlyRegions, const QString &dayTitle,
	             QString *error);

	/// 输出目录（`dist/admission/`）。
	static QString outputRoot();
	/// 模板缺失 / 损坏之类的启动检查；顺带检查 pandoc / xelatex。
	static QString toolsReport();

  signals:
	void logMessage(const QString &message);
	void progress(int done, int total);

  private:
	bool cancelled{false};
};
