/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

class Contest;
class Contestant;

/**
 * 把比赛日打包成各平台要的 .zip。界面上的「导出」选项卡与命令行入口共用这一套逻辑。
 *
 * 目前有两种包：
 *
 * - **选手目录**（ContestantPackage）：每道题一个子目录（子目录名 = 题目目录名），
 *   里面放该题 `problem/<题>/down/` 下的文件（**不含 down 子目录里的文件**）；
 *   整场的题面文件（`statement/` 下用户选定的那一个）放在压缩包根目录。
 * - **测试数据**（TestDataPackage）：把每题的 `data/`（**只含测试点真正引用到的
 *   输入 / 输出文件**，路径来自 .cdf）、`graders/`（可选 `down/`）
 *   按开关决定是保留原来的目录结构还是铺平，也可以决定要不要每道题单独一个目录。
 * - **选手代码**（AnswersPackage）：每位选手一个目录，里面是他全部题目的源代码；
 *   启用赛区时按赛区分组（同名文件夹 / 每个赛区一个同名内层压缩包），
 *   固定输出 `<比赛日>/dist/export/answers.zip`。
 *
 * 可选开关（不适用的包会忽略）：
 * - wrapInFolder：多一层分组目录（选手目录/测试数据是 <比赛日>；选手代码是赛区名）；
 * - nestedZip：内容先打成内层压缩包（见下）；
 * - oneFolderPerTask / keepStructure / includeSamples：测试数据包用；
 * - password：非空时用 ZipCrypto（传统 zip 加密）加密，内外层用同一个密码。
 *
 * 两个通用开关都打开时：`<比赛日>.zip` 里只有一个内层压缩包，内层里是内容
 * （套层打开时再包一层 `<比赛日>/` 目录）。
 *
 * 以后要加其它平台时，在 Kind 里追加并补上 collectItems() 里的分支即可。
 */
class PackageBuilder : public QObject {
	Q_OBJECT

  public:
	/// 包类型（顺序与 kindNames() 一致）。
	enum Kind { ContestantPackage = 0, TestDataPackage = 1, AnswersPackage = 2 };

	/// 打包内容里的一项：压缩包内路径 ↔ 磁盘上的来源。
	struct Item {
		QString archivePath; ///< 压缩包内路径，用 `/` 分隔
		QString sourcePath;  ///< 磁盘上的绝对路径；isDirectory 为 true 时为空
		bool isDirectory{false};
	};

	/// 打包计划：外层压缩包的条目，以及要套在里面的内层压缩包。
	struct NestedArchive {
		QString name;       ///< 内层压缩包的名字（如 down.zip / day1.zip / HN.zip）
		QList<Item> items;  ///< 里面的条目
	};

	struct Plan {
		QList<Item> items;              ///< 外层压缩包里的普通条目
		QList<NestedArchive> archives;  ///< 内层压缩包（外层里就是这些 zip 文件）
	};

	explicit PackageBuilder(QObject *parent = nullptr);

	/// 所有包类型的显示名。
	static QStringList kindNames();
	/// 越界的下标当成第一种包。
	static Kind kindAt(int index);

	void setKind(Kind);
	Kind kind() const { return packageKind; }

	/// 比赛日文件（.cdf）。它的所在目录就是比赛日目录，文件名用来命名套层目录与默认 zip。
	void setDayFile(const QString &);
	/// 只给比赛日目录时用它；一般用 setDayFile() 就够了。
	void setDayDirectory(const QString &);
	/// 题目列表从这里取；只在真正打包时读一次，永远是当前状态。
	void setContest(Contest *);
	/// 输出的 .zip（绝对路径）。
	void setOutputFile(const QString &);

	/// 全部内容再套一层与比赛日文件同名的目录。
	void setWrapInFolder(bool);
	bool wrapInFolder() const { return wrap; }
	/// 全部内容打成内层压缩包（名字见 innerArchiveName()），外层压缩包里只有它。
	void setNestedZip(bool);
	bool nestedZip() const { return nested; }
	/// 测试数据包：每道题单独一个目录（默认开启；各题数据文件重名时不能关）。
	void setOneFolderPerTask(bool);
	bool oneFolderPerTask() const { return perTask; }
	/// 测试数据包：保留 data/ graders/ down/ 这些内部目录结构（默认开启）。
	void setKeepStructure(bool);
	bool keepStructure() const { return structure; }
	/// 测试数据包：一并导出样例数据（down/）。开启时强制保留内部结构。
	void setIncludeSamples(bool);
	bool includeSamples() const { return samples; }
	/// 选手目录包：要放进包里的题面文件（`statement/` 下的文件名，如 `statement.pdf`）。
	/// 空表示用默认的 statement.pdf。
	void setStatementFile(const QString &);
	QString statementFile() const;
	/// 内层压缩包的名字：选手目录固定 down.zip，测试数据与比赛日文件同名。
	QString innerArchiveName() const;
	/// 测试数据包：各题的文件是否重名（重名就不能关掉「每道题单独一个目录」）。
	bool hasDuplicateTaskFiles();
	/// 密码（空表示不加密）。
	void setPassword(const QString &);
	QString password() const { return pass; }

	/// 比赛日名（套层目录名 / 默认 zip 名）：取比赛日文件名，没有文件时取目录名。
	QString dayName() const;
	/// 比赛日目录（绝对路径）。
	QString dayRoot() const;

	/// 默认输出文件：`<比赛日>/dist/export/` 下，选手目录叫 `<比赛日>.zip`，
	/// 测试数据叫 `evaldata.zip`，选手代码叫 `answers.zip`。
	QString defaultOutputFile() const;

	/// 列出会打进包里的条目（不写文件），供界面预览。缺失的东西会在日志里提醒。
	Plan collectItems();

	/// 真正写 zip；失败时 lastError() 给原因。
	bool build();

	/// 清空内部缓存（磁盘上的数据可能变了）：打包前、点「刷新」时会调。
	void clearCache();

	QString lastError() const { return error; }

  signals:
	void logMessage(const QString &);

  private:
	bool collectContestantPackage(QList<Item> &items);
	bool collectTestDataPackage(QList<Item> &items);
	bool collectAnswersPackage(Plan &plan);
	/// 把一道题 down 目录里的文件收集成 (包内名字, 磁盘路径) 列表。
	QList<QPair<QString, QString>> collectDownFiles(const QString &taskName);
	/// 测试数据包：一道题要打包的文件（相对题目录的路径, 磁盘路径）。
	QList<QPair<QString, QString>> collectTestDataFiles(const class Task *task, bool quiet);
	/// 一道题要带上的 graders/ 文件（文件名, 磁盘路径）：交互库 / 通信库 / 校验器源码。
	QList<QPair<QString, QString>> graderFilesForTask(const class Task *task, bool quiet);
	/// 与打包范围有关的题目配置（题型、判题方式、交互库 / 校验器路径），用作缓存键的一部分。
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
	QString statement;
	QString pass;
	QString error;

	/// collectItems() / hasDuplicateTaskFiles() 都要遍历磁盘，按「输入签名」缓存起来：
	/// 来回切包类型、把选项改回原样，都不用重新扫一遍。
	QHash<QString, Plan> planCache;
	QHash<QString, bool> duplicatesCache;
	/// 测试数据包：一道题的文件列表（键里带上结构 / 样例开关、题目配置、比赛日）。
	QHash<QString, QList<QPair<QString, QString>>> taskFileCache;

	/// 当前输入的签名：包类型 + 各选项 + 比赛日。
	QString inputKey() const;
};
