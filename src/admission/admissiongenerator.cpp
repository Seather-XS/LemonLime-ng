/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "admissiongenerator.h"
//
#include "admissioncolumns.h"
#include "admissionnaming.h"
#include "admissionnotes.h"
#include "admissionproject.h"
#include "admissiontemplate.h"
#include "base/LemonLog.hpp"
#include "base/ProcessUtil.hpp"
#include "base/settings.h"
//
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtCore/private/qzipwriter_p.h>

#define LEMON_MODULE_NAME "Admission"

namespace {
	QString native(const QString &path) { return QDir::fromNativeSeparators(path); }

	QString toolPath(const QString &name) { return QStandardPaths::findExecutable(name); }

	/// 纯文本进 LaTeX 前转义（真正的实现在 AdmissionNotes 里）。
	QString escapeLatex(const QString &value) { return AdmissionNotes::escape(value).trimmed(); }

	/// 文件名安全化：去掉 Windows 不允许的字符，限长，空的话给个兜底。
	QString sanitize(const QString &name, const QString &fallback = QStringLiteral("ticket")) {
		static const QRegularExpression illegal(QStringLiteral(R"([\\/:*?"<>|\x00-\x1F])"));
		QString out = name.trimmed();
		out.remove(illegal);

		while (out.endsWith(QChar('.')) || out.endsWith(QChar(' ')))
			out.chop(1);

		if (out.size() > 64)
			out = out.left(64);

		return out.isEmpty() ? fallback : out;
	}

	/// 照片路径：绝对路径直接用，否则先按赛区目录找，再按比赛日目录找。
	QString resolvePhoto(const QString &value, const QString &region) {
		const QString trimmed = value.trimmed();

		if (trimmed.isEmpty())
			return QString();

		if (QFileInfo(trimmed).isAbsolute())
			return QFileInfo::exists(trimmed) ? trimmed : QString();

		const QString inRegion = AdmissionProject::regionFolder(region) + trimmed;

		if (QFileInfo::exists(inRegion))
			return inRegion;

		return QFileInfo::exists(trimmed) ? trimmed : QString();
	}

	QString readTextFile(const QString &path) {
		QFile file(path);

		if (! file.open(QIODevice::ReadOnly))
			return {};

		QString text = QString::fromUtf8(file.readAll());
		text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		return text;
	}

	bool writeTextFile(const QString &path, const QString &text, QString *error) {
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile file(path);

		if (! file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			if (error)
				*error = QObject::tr("Cannot write %1").arg(path);
			return false;
		}

		file.write(text.toUtf8());
		file.close();
		return true;
	}

	bool copyPath(const QString &from, const QString &to) {
		const QFileInfo info(from);

		if (info.isDir()) {
			QDir().mkpath(to);

			for (const QString &name : QDir(from).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
				if (! copyPath(from + QChar('/') + name, to + QChar('/') + name))
					return false;

			return true;
		}

		QDir().mkpath(QFileInfo(to).absolutePath());
		QFile::remove(to);
		return QFile::copy(from, to);
	}

	int runTool(const QString &program, const QStringList &arguments, const QString &workingDirectory,
	            const QString &logTailPath) {
		QProcess process;
		Lemon::common::suppressConsoleWindow(process);
		process.setWorkingDirectory(workingDirectory);
		process.setProcessChannelMode(QProcess::SeparateChannels);
		process.start(program, arguments);

		if (! process.waitForStarted(15000))
			return -1;

		if (! process.waitForFinished(300000)) {
			process.kill();
			process.waitForFinished(3000);
			return -2;
		}

		if (process.exitCode() != 0 && ! logTailPath.isEmpty()) {
			writeTextFile(logTailPath,
			              QString::fromLocal8Bit(process.readAllStandardOutput()) +
			                  QString::fromLocal8Bit(process.readAllStandardError()),
			              nullptr);
		}

		return process.exitCode();
	}

	/// 从 xelatex 的 .log 里挑出报错段落。
	QString analyzeLog(const QString &path) {
		const QString text = readTextFile(path);

		if (text.isEmpty())
			return {};

		QStringList blocks;
		QStringList current;

		for (const QString &line : text.split(QChar('\n'))) {
			if (line.startsWith(QChar('!'))) {
				if (! current.isEmpty())
					blocks << current.join(QChar('\n')).trimmed();

				current = {line};
			} else if (! current.isEmpty()) {
				if (line.trimmed().isEmpty()) {
					blocks << current.join(QChar('\n')).trimmed();
					current.clear();
				} else {
					current << line;
				}
			}
		}

		if (! current.isEmpty())
			blocks << current.join(QChar('\n')).trimmed();

		return blocks.join(QStringLiteral("\n\n")).trimmed();
	}
} // namespace

AdmissionGenerator::AdmissionGenerator(QObject *parent) : QObject(parent) {}

auto AdmissionGenerator::outputRoot() -> QString {
	return native(QFileInfo(Settings::admissionOutputPath()).absoluteFilePath());
}

auto AdmissionGenerator::toolsReport() -> QString {
	QStringList missing;

	if (toolPath(QStringLiteral("xelatex")).isEmpty())
		missing << QStringLiteral("xelatex");

	if (AdmissionTemplate::source().isEmpty())
		missing << QStringLiteral("templates/admission/main.tex");

	return missing.isEmpty() ? QString() : missing.join(QStringLiteral(", "));
}

int AdmissionGenerator::generate(AdmissionProject &project, const QStringList &onlyRegions,
                                const QString &dayTitle, QString *error) {
	cancelled = false;

	if (error)
		error->clear();

	const QString templateText = AdmissionTemplate::source();

	if (templateText.isEmpty()) {
		if (error)
			*error = tr("Cannot find the built-in admission template (templates/admission/main.tex).");
		return -1;
	}

	QString templateError;

	if (! AdmissionTemplate::validate(templateText, &templateError)) {
		if (error)
			*error = templateError;
		return -1;
	}

	const QString xelatex = toolPath(QStringLiteral("xelatex"));

	if (xelatex.isEmpty()) {
		if (error)
			*error = tr("xelatex not found in PATH.");
		return -1;
	}

	emit logMessage(tr("xelatex: %1").arg(xelatex));

	QStringList regions = onlyRegions;

	if (regions.isEmpty())
		for (const AdmissionRegion &region : project.regions)
			regions << region.name;

	int total = 0;

	for (const QString &name : std::as_const(regions)) {
		const AdmissionRegion *region = project.find(name);

		if (region)
			total += region->table.rows.size();
	}

	if (total == 0) {
		if (error)
			*error = tr("Nothing to generate: the lists are empty.");
		return -1;
	}

	const QString outputRoot = AdmissionGenerator::outputRoot();

	if (project.overwrite && QFileInfo::exists(outputRoot))
		QDir(outputRoot).removeRecursively();

	QDir().mkpath(outputRoot);

	QTemporaryDir sandbox;

	if (! sandbox.isValid()) {
		if (error)
			*error = tr("Cannot create a temporary directory.");
		return -1;
	}

	const QString fonts = AdmissionTemplate::fontDir();
	const QString title = project.title.trimmed().isEmpty() ? dayTitle : project.title;
	int done = 0;
	int failed = 0;
	int globalSeq = 0;

	emit progress(0, total);

	for (const QString &name : std::as_const(regions)) {
		const AdmissionRegion *region = project.find(name);

		if (! region || cancelled)
			continue;

		const QString label = region->name.isEmpty() ? tr("(no region)") : region->name;
		QString notesError;

		// 生成列 / 随机列：按列序求值（引用必须指向已经求值的列）
		AdmissionTable table = region->table;
		QList<AdmissionColumnRule> rules;

		if (! AdmissionColumns::load(region->name, rules, &notesError)) {
			emit logMessage(tr("%1: %2").arg(label, notesError));
		} else if (! rules.isEmpty() &&
		           ! AdmissionColumns::evaluate(rules, table, region->name, true, &notesError)) {
			emit logMessage(tr("%1: column generation failed: %2").arg(label, notesError));
		}

		// 注意事项 / 比赛注意：只认 [文字](链接)，其余当纯文本
		const QString notesLatex = AdmissionNotes::toLatex(region->notes);
		const QString generalLatex = AdmissionNotes::toLatex(project.contestNotes);

		const QString regionOut = outputRoot +
		                          ((project.dirLayout == QStringLiteral("byRegion") && ! region->name.isEmpty())
		                               ? QChar('/') + sanitize(region->name, QStringLiteral("region"))
		                               : QString());
		QDir().mkpath(regionOut);
		QSet<QString> usedNames;
		int regionOk = 0;
		int regionFailed = 0;
		QElapsedTimer regionTimer;
		regionTimer.start();
		emit logMessage(tr("%1: %2 contestant(s) → %3")
		                    .arg(label)
		                    .arg(table.rows.size())
		                    .arg(QDir::toNativeSeparators(regionOut)));

		for (int row = 0; row < table.rows.size(); ++row) {
			if (cancelled)
				break;

			const QStringList values = table.rowAt(row);
			auto csv = [&table, &values](const QString &column) {
				const int index = table.columnIndex(column);
				return index >= 0 ? values.value(index).trimmed() : QString();
			};
			const QString name = csv(QStringLiteral("姓名"));
			const QString id = csv(QStringLiteral("准考证号"));
			const QString seat = csv(QStringLiteral("座位号"));
			const QString venue = csv(QStringLiteral("考点"));
			const QString room = csv(QStringLiteral("考场"));

			if (id.isEmpty()) {
				++failed;
				++done;
				++regionFailed;
				emit logMessage(tr("%1: row %2 has no ticket number, skipped.").arg(label).arg(row + 2));
				emit progress(done, total);
				continue;
			}

			++globalSeq;

			// 输出文件名：启用了命名规则就按规则渲染，否则用准考证号
			AdmissionNamingContext namingContext;
			namingContext.section = region->name;
			namingContext.name = name;
			namingContext.id = id;
			namingContext.seat = seat;
			namingContext.room = room;
			namingContext.row = row + 1;
			namingContext.regionSeq = row + 1;
			namingContext.globalSeq = globalSeq;

			for (int index = 0; index < table.header.size(); ++index)
				namingContext.columns.insert(table.header.at(index), values.value(index).trimmed());

			QString base;

			if (project.namingEnabled && ! project.namingTemplate.trimmed().isEmpty()) {
				QString namingError;
				base = AdmissionNaming::sanitize(AdmissionNaming::render(project.namingTemplate, namingContext,
				                                                        project.namingNumberSources,
				                                                        project.namingCharSources, &namingError),
				                                project.namingReplacement, project.namingMaxLength);

				if (! namingError.isEmpty())
					emit logMessage(tr("%1: naming rule: %2").arg(label, namingError));
			}

			if (base.trimmed().isEmpty())
				base = sanitize(id);

			if (base.trimmed().isEmpty())
				base = QStringLiteral("ticket");

			// 额外行 = 名单里锁定列之外的自定义列：按「列名 | 该行的值」印在锁定行之后。
			// 单元格里同样可以写占位符（<row> <name> <id> <seat> <room> 以及其它列名）。
			QString extraRowsLatex;

			if (table.header.size() > AdmissionTable::builtinColumns().size()) {
				AdmissionNamingContext extraContext;
				extraContext.section = region->name;
				extraContext.name = name;
				extraContext.id = id;
				extraContext.seat = seat;
				extraContext.room = room;
				extraContext.row = row + 1;
				extraContext.regionSeq = row + 1;
				extraContext.globalSeq = globalSeq;

				for (int index = 0; index < table.header.size(); ++index)
					extraContext.columns.insert(table.header.at(index), values.value(index).trimmed());

				QStringList lines;

				for (int index = 0; index < table.header.size(); ++index) {
					const QString column = table.header.at(index);

					if (AdmissionTable::builtinColumns().contains(column))
						continue;

					QString text = values.value(index).trimmed();

					if (text.isEmpty())
						continue;

					text.replace(QStringLiteral("<section>"), extraContext.section);
					text.replace(QStringLiteral("<name>"), extraContext.name);
					text.replace(QStringLiteral("<id>"), extraContext.id);
					text.replace(QStringLiteral("<seat>"), extraContext.seat);
					text.replace(QStringLiteral("<room>"), extraContext.room);
					text.replace(QStringLiteral("<row>"), QString::number(extraContext.row));

					for (auto it = extraContext.columns.constBegin(); it != extraContext.columns.constEnd(); ++it)
						text.replace(QChar('<') + it.key() + QChar('>'), it.value());

					QString rendered = AdmissionNotes::toLatex(text);

					lines << QStringLiteral("\\hline\n") + escapeLatex(column) + QStringLiteral(" & ") +
					             QStringLiteral("\\multicolumn{2}{p{13cm}!{\\vrule width 1.2pt}}{") + rendered +
					             QStringLiteral("} \\\\");
				}

				extraRowsLatex = lines.join(QChar('\n'));
			}
			const QString ticketOut = regionOut +
			                          ((project.dirLayout == QStringLiteral("byRoom") && ! room.isEmpty())
			                               ? QChar('/') + sanitize(room, QStringLiteral("room"))
			                               : QString());
			QDir().mkpath(ticketOut);

			// 沙箱：每个选手一个干净目录（xelatex 的中间产物都落在这里）
			const QString work = native(sandbox.path()) + QStringLiteral("/ticket");
			QDir(work).removeRecursively();
			QDir().mkpath(work);

			if (! fonts.isEmpty())
				copyPath(fonts, work + QStringLiteral("/fonts"));

			// 照片：列表里给的是路径（相对赛区目录或比赛日目录都行），拷进沙箱后按比例放进右侧方框。
			const QString photoInput = csv(QStringLiteral("照片"));
			QString photoLatex;

			if (! photoInput.trimmed().isEmpty()) {
				const QString source = resolvePhoto(photoInput, region->name);
				const QString suffix = QFileInfo(source).suffix().toLower();
				const QString photoName = QStringLiteral("photo.") + suffix;

				if (source.isEmpty() || suffix.isEmpty() ||
				    ! QFile::copy(source, work + QChar('/') + photoName))
					emit logMessage(tr("%1: cannot read the photo %2").arg(label, photoInput));
				else
					photoLatex = QStringLiteral("\\includegraphics[width=2.9cm,height=3.6cm,keepaspectratio]{") +
					             photoName + QStringLiteral("}");
			}

			QMap<QString, QString> map;
			map.insert(QStringLiteral("title"), escapeLatex(title));
			map.insert(QStringLiteral("name"), escapeLatex(name));
			map.insert(QStringLiteral("id"), escapeLatex(id));
			map.insert(QStringLiteral("seat"), escapeLatex(seat));
			map.insert(QStringLiteral("examTime"), escapeLatex(project.examTime));
			map.insert(QStringLiteral("venue"), escapeLatex(venue));
			map.insert(QStringLiteral("room"), escapeLatex(room));
			map.insert(QStringLiteral("photo"), photoLatex);
			map.insert(QStringLiteral("notes"), notesLatex);
			map.insert(QStringLiteral("contestNotes"), generalLatex);
			map.insert(QStringLiteral("extraRows"), extraRowsLatex);
			map.insert(QStringLiteral("beforeTable"), QString());
			map.insert(QStringLiteral("afterTable"), QString());

			QStringList unresolved;
			const QString document = AdmissionTemplate::render(templateText, map, &unresolved);

			if (! unresolved.isEmpty())
				emit logMessage(tr("%1: template placeholders with no value: %2")
				                    .arg(label, unresolved.join(QStringLiteral(" "))));

			if (! writeTextFile(work + QStringLiteral("/main.tex"), document, nullptr)) {
				++failed;
				++regionFailed;
			} else {
				int code = runTool(xelatex, {QStringLiteral("-no-shell-escape"),
				                             QStringLiteral("-interaction=nonstopmode"),
				                             QStringLiteral("-halt-on-error"), QStringLiteral("main.tex")},
				                   work, QString());

				if (code == 0)
					code = runTool(xelatex, {QStringLiteral("-no-shell-escape"),
					                         QStringLiteral("-interaction=nonstopmode"),
					                         QStringLiteral("-halt-on-error"), QStringLiteral("main.tex")},
					               work, QString());

				QString unique = base;

				while (usedNames.contains(unique))
					unique += QStringLiteral("_");

				usedNames.insert(unique);
				const QString who = name.isEmpty() ? id : name + QStringLiteral(" (") + id + QChar(')');

				if (code == 0 && QFileInfo::exists(work + QStringLiteral("/main.pdf"))) {
					const QString target = ticketOut + QChar('/') + unique + QStringLiteral(".pdf");
					QFile::remove(target);

					if (QFile::copy(work + QStringLiteral("/main.pdf"), target)) {
						++regionOk;
						emit logMessage(tr("%1: %2 → %3")
						                    .arg(label, who, QFileInfo(target).fileName()));
					} else {
						++failed;
						++regionFailed;
						emit logMessage(tr("%1: cannot write %2").arg(label, target));
					}
				} else {
					++failed;
					++regionFailed;
					// 失败时把 .tex / .log 留一份，方便照着报错改名单或环境
					const QString keep = outputRoot + QStringLiteral("/_failed");
					QDir().mkpath(keep);
					QFile::remove(keep + QChar('/') + unique + QStringLiteral(".tex"));
					QFile::remove(keep + QChar('/') + unique + QStringLiteral(".log"));
					QFile::copy(work + QStringLiteral("/main.tex"),
					            keep + QChar('/') + unique + QStringLiteral(".tex"));
					QFile::copy(work + QStringLiteral("/main.log"),
					            keep + QChar('/') + unique + QStringLiteral(".log"));
					emit logMessage(tr("%1: %2 failed to compile: %3")
					                    .arg(label, who, analyzeLog(work + QStringLiteral("/main.log"))));
				}
			}

			++done;
			emit progress(done, total);
		}

		// 按赛区打包
		if (project.packageByRegion && ! cancelled && QFileInfo::exists(regionOut)) {
			const QString zipName =
			    project.packageName.isEmpty() ? QStringLiteral("admission-{region}") : project.packageName;
			const QString zipPath =
			    outputRoot + QChar('/') +
			    sanitize(QString(zipName).replace(QStringLiteral("{region}"),
			                                      sanitize(region->name, QStringLiteral("all")))) +
			    QStringLiteral(".zip");
			QZipWriter writer(zipPath);
			int count = 0;

			for (const QFileInfo &info :
			     QDir(regionOut).entryInfoList({QStringLiteral("*.pdf")}, QDir::Files, QDir::Name)) {
				QFile file(info.absoluteFilePath());

				if (! file.open(QIODevice::ReadOnly))
					continue;

				writer.addFile(info.fileName(), file.readAll());
				file.close();
				++count;
			}

			writer.close();

			if (count > 0)
				emit logMessage(tr("%1: %2 written (%3 files)").arg(label, QFileInfo(zipPath).fileName()).arg(count));
		}

		emit logMessage(tr("%1: %2 ok, %3 failed, %4 s")
		                    .arg(label)
		                    .arg(regionOk)
		                    .arg(regionFailed)
		                    .arg(regionTimer.elapsed() / 1000.0, 0, 'f', 1));
	}

	return failed;
}
