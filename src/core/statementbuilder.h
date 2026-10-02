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
//
#include <atomic>

/**
 * 题面 PDF 构建引擎。
 *
 * 与工作区里的 statement-editor 使用同一套逻辑：markdown 先由 pandoc 转成 LaTeX，
 * 再按模板（ccpc / noi / noi new）拼装完整文档，最后用 xelatex 编译两遍得到 PDF。
 */
class StatementBuilder : public QObject {
	Q_OBJECT

  public:
	explicit StatementBuilder(QObject *parent = nullptr);

	/// 可选模板名（与 statement-editor 的 templates/ 目录一致）。
	static QStringList templateNames();
	/// 工程自带的模板根目录（含 ccpc / noi / noi new / fonts）；找不到时返回空串。
	static QString templateRoot();
	/// 检查 pandoc / xelatex 是否可用，返回可读的说明。
	static QString toolsReport();

	void setTemplate(const QString &name);
	void setSourceFile(const QString &path);
	/// 不带扩展名的输出路径，最终产出 `<outputBase>.pdf`。
	void setOutputBase(const QString &pathWithoutSuffix);

	/// 同步构建；失败时 `lastError()` 给出原因，构建日志通过 logMessage() 发出。
	bool build();
	/// 是否正在构建（供界面禁用按钮用）。可能从其他线程读写，所以用原子量。
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
