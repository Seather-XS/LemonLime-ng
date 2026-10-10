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

#include "base/LemonType.hpp"
#include <QColor>
#include <QCoreApplication>
#include <QObject>

class Compiler;

struct hslTuple {
	int h;
	double s;
	double l;
	hslTuple(int _h = 0, double _s = 0.00, double _l = 0.00) : h(_h), s(_s), l(_l) {}
	const QColor toHsl() const { return QColor::fromHslF(h / 360.0, s / 100.0, l / 100.0); }
};
Q_DECLARE_METATYPE(hslTuple)

struct dddTuple {
	double h;
	double s;
	double l;
	dddTuple(double _h = 0, double _s = 0.00, double _l = 0.00) : h(_h), s(_s), l(_l) {}
};
Q_DECLARE_METATYPE(dddTuple)

class ColorTheme {
	Q_GADGET
  public:
	explicit ColorTheme();

	void setName(QString);
	void setColor(const hslTuple &, const hslTuple &, const hslTuple &, const hslTuple &, const dddTuple &,
	              const dddTuple &);
	void copyFrom(ColorTheme *);

	QString getName() const;
	hslTuple getMxColor() const;
	hslTuple getMiColor() const;
	hslTuple getNfColor() const;
	hslTuple getCeColor() const;
	dddTuple getGrandComp() const;
	dddTuple getGrandRate() const;

	QColor getColorNf() const;
	QColor getColorCe() const;
	QColor getColorPer(double) const;
	QColor getColorGrand(double) const;
	QColor getColorPer(double, double) const;
	QColor getColorGrand(double, double) const;

	void invertLightness();

  private:
	QString name;
	// mxColor maxColor
	// miColor minColor
	// nfColor nofileColor
	// ceColor compileErrorColor
	hslTuple mxColor, miColor, nfColor, ceColor;
	QColor colorNf, colorCe;
	dddTuple grandComp, grandRate;
};

class Settings {
	Q_DECLARE_TR_FUNCTIONS(Settings)
  public:
	explicit Settings();

	int getDefaultFullScore() const;
	int getDefaultTimeLimit() const;
	int getDefaultMemoryLimit() const;
	int getCompileTimeLimit() const;
	int getSpecialJudgeTimeLimit() const;
	int getFileSizeLimit() const;
	int getRejudgeTimes() const;
	int getMaxJudgingThreads() const;
	/// CCF 编程通则 3：选手程序应正常结束且主函数返回 0（非 0 → 运行时错误）。
	bool getRequireReturnZero() const;
	double getDefaultExtraTimeRatio() const;
	const QString &getDefaultInputFileExtension() const;
	const QString &getDefaultOutputFileExtension() const;
	const QStringList &getInputFileExtensions() const;
	const QStringList &getOutputFileExtensions() const;
	const QStringList &getRecentContest() const;
	const QList<Compiler *> &getCompilerList() const;
	const QList<ColorTheme *> &getColorThemeList() const;
	const QString &getUiLanguage() const;
	const QString &getDiffPath() const;
	int getSplashTime() const;

	void setDefaultFullScore(int);
	void setDefaultTimeLimit(int);
	void setDefaultExtraTimeRatio(double);
	void setDefaultMemoryLimit(int);
	void setCompileTimeLimit(int);
	void setSpecialJudgeTimeLimit(int);
	void setFileSizeLimit(int);
	void setRejudgeTimes(int);
	void setMaxJudgingThreads(int);
	void setRequireReturnZero(bool);
	void setDefaultInputFileExtension(const QString &);
	void setDefaultOutputFileExtension(const QString &);
	void setInputFileExtensions(const QString &);
	void setOutputFileExtensions(const QString &);
	void setRecentContest(const QStringList &);
	void setUiLanguage(const QString &);
	void setSplashTime(int);

	void addCompiler(Compiler *);
	void deleteCompiler(int);
	Compiler *getCompiler(int);
	void swapCompiler(int, int);

	void addColorTheme(ColorTheme *);
	void deleteColorTheme(int);
	ColorTheme *getColorTheme(int);
	ColorTheme getCurrentColorTheme() const;
	int getCurrentColorThemeIndex() const;

	void setColorTheme(ColorTheme *, int);
	void setCurrendColorTheme(ColorTheme *);
	void setCurrentColorThemeIndex(int);

	void copyFrom(Settings *);
	void saveSettings();
	void loadSettings();

	static void setTextAndColor(ResultState, QString &, QString &, QString &);
	static int upperBoundForFullScore();
	static int upperBoundForTimeLimit();
	static int upperBoundForMemoryLimit();
	static int upperBoundForFileSizeLimit();
	static int upperBoundForRejudgeTimes();
	static double upperBoundForExtraTimeRatio();
	static QString dataPath();
	static QString sourcePath();
	static QString importPath();
	static QString selfTestPath();
	/// 题面目录：每个比赛日下的 `statement/`（固定放 statement.md 与 statement.pdf）。
	static QString statementPath();
	/// 导出目录：每个比赛日下的 `dist/reports/`（成绩单、统计）与 `dist/export/`（压缩包）。
	static QString reportsPath();
	static QString exportPath();
	/// 准考证：名单 / 注意事项 / 模板放 `admission/`，生成的 PDF 放 `dist/admission/`。
	static QString admissionPath();
	static QString admissionOutputPath();
	/// 建好 dist/reports/、dist/export/ 与 dist/admission/，并清掉早期版本留在比赛日根目录的 reports/、export/。
	static void ensureDistDirs();
	/// 交互库 / 校验器只允许放在 <题>/graders/ 下，返回相对 dataPath() 的该目录（带结尾分隔符）。
	static QString gradersPath(const QString &taskName);
	/// <题>/graders/<文件>（相对 dataPath()），与工程中保存交互库路径的格式一致。
	static QString graderFilePath(const QString &taskName, const QString &fileName);
	/// 建立某个试题的标准子目录：problem/<题>/{data,down,graders,gen}；
	/// 顺便清掉早期版本留下的 tests/ 目录。
	static void ensureTaskDirs(const QString &taskName);

  private:
	QList<Compiler *> compilerList;
	int defaultFullScore{};
	int defaultTimeLimit{};
	int defaultMemoryLimit{};
	int compileTimeLimit{};
	int specialJudgeTimeLimit{};
	int fileSizeLimit{};
	int rejudgeTimes{};
	int maxJudgingThreads{};
	bool requireReturnZero{true};
	double defaultExtraTimeRatio{};
	QString defaultInputFileExtension;
	QString defaultOutputFileExtension;
	QStringList inputFileExtensions;
	QStringList outputFileExtensions;
	QStringList recentContest;
	QString uiLanguage;
	QString diffPath;
	QList<ColorTheme *> colorThemeList;
	int currentColorTheme{};

	int splashTime{};
};
