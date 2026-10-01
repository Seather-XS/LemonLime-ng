/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include <QCompleter>
#include <QDir>
#include <QLineEdit>
#include <QStringList>

class FileLineEdit : public QLineEdit {
	Q_OBJECT
  public:
	explicit FileLineEdit(QWidget *parent = nullptr);
	void setFilters(QDir::Filters);
	void setFileExtensions(const QStringList &);
	/// 把候选文件限制在 dataPath() 下的某个子目录里（传空表示不限制）。
	void setRootDirectory(const QString &relativeDir);
	void getFiles(const QString &, const QString &, QStringList &);

  private:
	QCompleter *completer;
	QStringList nameFilters;
	QDir::Filters filters;
	QString rootDirectory; ///< 相对 dataPath()，带结尾分隔符

  public slots:
	void refreshFileList();
};
