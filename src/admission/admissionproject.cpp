/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "admissionproject.h"
//
#include "base/LemonLog.hpp"
#include "base/settings.h"
//
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>

#define LEMON_MODULE_NAME "Admission"

namespace {
	/// 钉定的 admission/ 根目录（绝对路径）。空的 = 用 Settings::admissionPath()（相对工作目录）。
	QString admissionRootOverride;

	QString sep() { return QString(QDir::separator()); }

	bool readText(const QString &path, QString &text) {
		QFile file(path);

		if (! file.open(QIODevice::ReadOnly))
			return false;

		text = QString::fromUtf8(file.readAll());
		text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		text.replace(QChar('\r'), QChar('\n'));

		if (text.startsWith(QChar(0xFEFF)))
			text.remove(0, 1);

		file.close();
		return true;
	}

	bool writeText(const QString &path, const QString &text, QString *error) {
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

	/// 抽掉开头那一块「键: 值」赛区信息，返回剩下的正文。
	QString splitInfo(QString &markdown, QMap<QString, QString> &info) {
		static const QRegularExpression entry(QStringLiteral(R"(^\s{0,3}([^\s:：#][^:：]{0,29})\s*[:：]\s*(.*)$)"));
		static const QRegularExpression heading(QStringLiteral(R"(^\s{0,3}#{1,6}\s+)"));
		QStringList body;
		bool seenHeading = false;

		for (const QString &line : markdown.split(QChar('\n'))) {
			if (! seenHeading && heading.match(line).hasMatch())
				seenHeading = true;

			const QRegularExpressionMatch match = seenHeading ? QRegularExpressionMatch() : entry.match(line);

			if (! seenHeading && match.hasMatch()) {
				info.insert(match.captured(1).trimmed(), match.captured(2).trimmed());
				continue;
			}

			body << line;
		}

		return body.join(QChar('\n')).trimmed();
	}

	/// 取 `# 名字` 那一节；整篇没有标题时（fallback）全部算这一节。
	QString section(const QString &markdown, const QString &name, bool fallback) {
		static const QRegularExpression heading(QStringLiteral(R"(^\s{0,3}(#{1,6})\s+(.*?)\s*$)"));
		const QStringList lines = markdown.split(QChar('\n'));
		bool anyHeading = false;
		bool inside = false;
		int level = 0;
		QStringList kept;

		for (const QString &line : lines) {
			const QRegularExpressionMatch match = heading.match(line);

			if (match.hasMatch()) {
				anyHeading = true;
				const int current = match.captured(1).length();
				QString title = match.captured(2).trimmed();
				title.remove(QRegularExpression(QStringLiteral(R"([：:]\s*$)")));

				if (inside && current <= level)
					break;

				if (! inside && title == name) {
					inside = true;
					level = current;
				}

				continue;
			}

			if (inside)
				kept << line;
		}

		if (inside)
			return kept.join(QChar('\n')).trimmed();

		if (! anyHeading && fallback)
			return markdown.trimmed();

		return {};
	}

	/// 把一段通告写成 .md 文件的内容（两端留一个空行，方便手改）。
	QString composeNotes(const QString &markdown) {
		QString text = markdown.trimmed();
		return text.isEmpty() ? QString() : text + QChar('\n');
	}

	bool moveFile(const QString &from, const QString &to, QStringList *log) {
		QDir().mkpath(QFileInfo(to).absolutePath());

		if (QFileInfo::exists(to))
			QFile::remove(to);

		if (! QFile::rename(from, to)) {
			if (! QFile::copy(from, to))
				return false;
			QFile::remove(from);
		}

		if (log)
			*log << QStringLiteral("moved %1 -> %2").arg(QFileInfo(from).fileName(), QFileInfo(to).fileName());

		return true;
	}
} // namespace

auto AdmissionProject::root() -> QString {
	return admissionRootOverride.isEmpty() ? Settings::admissionPath() : admissionRootOverride;
}

void AdmissionProject::useRoot(const QString &path) {
	admissionRootOverride = path.trimmed().isEmpty()
	                            ? QString()
	                            : QDir::cleanPath(path) + sep();
}

auto AdmissionProject::regionsRoot() -> QString { return root() + QStringLiteral("regions") + sep(); }

auto AdmissionProject::configPath() -> QString { return root() + QStringLiteral("config.json"); }

auto AdmissionProject::contestNotesPath() -> QString { return root() + QStringLiteral("contest-notes.md"); }

auto AdmissionProject::regionFolder(const QString &region) -> QString {
	return region.trimmed().isEmpty() ? regionsRoot() : regionsRoot() + region.trimmed() + sep();
}

/// 旧数据的兼容路径：老版把测试时间写在赛区的 region.json 里。
auto AdmissionProject::regionJsonPath(const QString &region) -> QString {
	return regionFolder(region) + QStringLiteral("region.json");
}

auto AdmissionProject::notesPath(const QString &region) -> QString {
	return regionFolder(region) + QStringLiteral("notes.md");
}

auto AdmissionProject::contestantsPath(const QString &region) -> QString {
	return regionFolder(region) + QStringLiteral("contestants.csv");
}

auto AdmissionProject::columnsPath(const QString &region) -> QString {
	return regionFolder(region) + QStringLiteral("columns.json");
}

auto AdmissionProject::roomsPath(const QString &region) -> QString {
	return regionFolder(region) + QStringLiteral("rooms.json");
}

auto AdmissionRegion::totalCapacity() const -> int {
	int total = 0;

	for (const AdmissionVenue &venue : venues)
		for (const AdmissionRoom &room : venue.rooms)
			total += qMax(1, room.capacity);

	return total;
}

auto AdmissionRegion::roomCount() const -> int {
	int count = 0;

	for (const AdmissionVenue &venue : venues)
		count += venue.rooms.size();

	return count;
}

auto AdmissionProject::venuesToJson(const QList<AdmissionVenue> &venues) -> QString {
	QJsonArray venueArray;

	for (const AdmissionVenue &venue : venues) {
		QJsonArray roomArray;

		for (const AdmissionRoom &room : venue.rooms) {
			QJsonObject item;
			item.insert(QStringLiteral("name"), room.name);
			item.insert(QStringLiteral("capacity"), qMax(1, room.capacity));
			roomArray.append(item);
		}

		QJsonObject item;
		item.insert(QStringLiteral("name"), venue.name);
		item.insert(QStringLiteral("rooms"), roomArray);
		venueArray.append(item);
	}

	QJsonObject root;
	root.insert(QStringLiteral("venues"), venueArray);
	return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

auto AdmissionProject::venuesFromJson(const QString &json) -> QList<AdmissionVenue> {
	QList<AdmissionVenue> venues;
	const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).object()
	                             .value(QStringLiteral("venues"))
	                             .toArray();

	for (const auto &value : array) {
		const QJsonObject object = value.toObject();
		AdmissionVenue venue;
		venue.name = object.value(QStringLiteral("name")).toString().trimmed();

		for (const auto &roomValue : object.value(QStringLiteral("rooms")).toArray()) {
			const QJsonObject roomObject = roomValue.toObject();
			AdmissionRoom room;
			room.name = roomObject.value(QStringLiteral("name")).toString().trimmed();
			room.capacity = qMax(1, roomObject.value(QStringLiteral("capacity")).toInt(1));

			if (! room.name.isEmpty())
				venue.rooms << room;
		}

		if (! venue.name.isEmpty())
			venues << venue;
	}

	return venues;
}

auto AdmissionProject::find(const QString &region) -> AdmissionRegion * {
	for (AdmissionRegion &item : regions)
		if (item.name == region)
			return &item;

	return nullptr;
}

auto AdmissionProject::find(const QString &region) const -> const AdmissionRegion * {
	for (const AdmissionRegion &item : regions)
		if (item.name == region)
			return &item;

	return nullptr;
}

auto AdmissionProject::discoverRegions(bool regionEnabled) -> QStringList {
	if (! regionEnabled)
		return {QString()};

	QStringList names;
	const QDir dir(regionsRoot());

	for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
		names << name;

	return names;
}

bool AdmissionProject::load(bool regionEnabled, QString *error) {
	if (error)
		error->clear();

	regions.clear();

	QString text;

	if (readText(configPath(), text)) {
		const QJsonObject config = QJsonDocument::fromJson(text.toUtf8()).object();
		title = config.value(QStringLiteral("title")).toString();
		examTime = config.value(QStringLiteral("examTime")).toString();
		const QJsonObject policy = config.value(QStringLiteral("generatePolicy")).toObject();
		dirLayout = policy.value(QStringLiteral("dirLayout")).toString(dirLayout);
		packageByRegion = policy.value(QStringLiteral("package")).toString(QStringLiteral("byRegion")) !=
		                  QStringLiteral("none");
		packageName = policy.value(QStringLiteral("packageName")).toString(packageName);
		overwrite = policy.value(QStringLiteral("overwrite")).toBool(overwrite);

		// 输出文件名规则
		const QJsonObject naming = policy.value(QStringLiteral("naming")).toObject();
		namingEnabled = naming.value(QStringLiteral("enabled")).toBool(namingEnabled);
		namingTemplate = naming.value(QStringLiteral("template")).toString(namingTemplate);
		namingReplacement = naming.value(QStringLiteral("replacement")).toString(namingReplacement);
		namingMaxLength = naming.value(QStringLiteral("maxLength")).toInt(namingMaxLength);
		namingNumberSources.clear();
		namingCharSources.clear();

		for (const auto &value : naming.value(QStringLiteral("numberGroups")).toArray())
			namingNumberSources << value.toObject().value(QStringLiteral("source")).toString();

		for (const auto &value : naming.value(QStringLiteral("charGroups")).toArray())
			namingCharSources << value.toObject().value(QStringLiteral("source")).toString();

		// 排座位策略
		const QJsonObject seat = policy.value(QStringLiteral("seat")).toObject();
		seatLayout = seat.value(QStringLiteral("layout")).toInt(seatLayout);
		seatOrder = seat.value(QStringLiteral("order")).toString(seatOrder);

		// 准考证号规则
		const QJsonObject rule = config.value(QStringLiteral("idRule")).toObject();
		idTemplate = rule.value(QStringLiteral("template")).toString(idTemplate);
		idCharSources.clear();

		for (const auto &value : rule.value(QStringLiteral("charGroups")).toArray())
			idCharSources << value.toObject().value(QStringLiteral("source")).toString();
	}

	if (readText(contestNotesPath(), text))
		contestNotes = text;

	for (const QString &name : discoverRegions(regionEnabled)) {
		AdmissionRegion region;
		region.name = name;

		// 旧版把测试时间写在赛区的 region.json 里：还没搬到 config.json 就先用它的。
		if (examTime.isEmpty() && readText(regionJsonPath(name), text)) {
			const QJsonObject object = QJsonDocument::fromJson(text.toUtf8()).object();
			examTime = object.value(QStringLiteral("examTime")).toString();
		}

		if (readText(notesPath(name), text))
			region.notes = text;

		if (readText(roomsPath(name), text))
			region.venues = venuesFromJson(text);

		region.table.header = AdmissionTable::builtinColumns();

		if (QFileInfo::exists(contestantsPath(name))) {
			QString readError;

			if (! AdmissionCsv::readFile(contestantsPath(name), region.table, &readError)) {
				if (error)
					*error = readError;
			}
		}

		region.table.normalize();
		regions << region;
	}

	return ! (error && ! error->isEmpty());
}

bool AdmissionProject::createRegion(const QString &region, QString *error) {
	AdmissionRegion item;
	item.name = region;
	item.table.header = AdmissionTable::builtinColumns();

	// 只有真正要落盘的文件才建出来：notes.md / contestants.csv / columns.json
	QDir().mkpath(regionFolder(region));

	if (! writeText(notesPath(region), QString(), error))
		return false;

	if (! writeText(roomsPath(region), venuesToJson({}), error))
		return false;

	if (! writeText(columnsPath(region), QStringLiteral("{\n\t\"columns\": []\n}\n"), error))
		return false;

	return AdmissionCsv::writeFile(contestantsPath(region), item.table, error);
}

bool AdmissionProject::removeRegion(const QString &region, QString *error) {
	const QString folder = regionFolder(region);
	QDir dir(folder);

	if (! dir.exists())
		return true;

	if (! dir.removeRecursively()) {
		if (error)
			*error = QObject::tr("Cannot delete %1").arg(QDir::toNativeSeparators(folder));

		return false;
	}

	return true;
}

bool AdmissionProject::save(QString *error) const {
	if (! saveConfig(error))
		return false;

	if (! writeText(contestNotesPath(), contestNotes, error))
		return false;

	for (const AdmissionRegion &region : regions) {
		if (! writeText(notesPath(region.name), composeNotes(region.notes), error))
			return false;

		if (! writeText(roomsPath(region.name), venuesToJson(region.venues), error))
			return false;

		if (! AdmissionCsv::writeFile(contestantsPath(region.name), region.table, error))
			return false;

		if (! QFileInfo::exists(columnsPath(region.name)))
			if (! writeText(columnsPath(region.name), QStringLiteral("{\n\t\"columns\": []\n}\n"), error))
				return false;
	}

	return true;
}

bool AdmissionProject::saveConfig(QString *error) const {
	QJsonObject policy;
	policy.insert(QStringLiteral("dirLayout"), dirLayout);
	policy.insert(QStringLiteral("package"), packageByRegion ? QStringLiteral("byRegion") : QStringLiteral("none"));
	policy.insert(QStringLiteral("packageName"), packageName);
	policy.insert(QStringLiteral("overwrite"), overwrite);

	// 输出文件名规则
	{
		QJsonArray numbers;
		QJsonArray chars;

		for (const QString &source : namingNumberSources) {
			QJsonObject item;
			item.insert(QStringLiteral("source"), source);
			numbers.append(item);
		}

		for (const QString &source : namingCharSources) {
			QJsonObject item;
			item.insert(QStringLiteral("source"), source);
			chars.append(item);
		}

		QJsonObject naming;
		naming.insert(QStringLiteral("enabled"), namingEnabled);
		naming.insert(QStringLiteral("template"), namingTemplate);
		naming.insert(QStringLiteral("numberGroups"), numbers);
		naming.insert(QStringLiteral("charGroups"), chars);
		naming.insert(QStringLiteral("onInvalid"), QStringLiteral("replace"));
		naming.insert(QStringLiteral("replacement"), namingReplacement);
		naming.insert(QStringLiteral("maxLength"), namingMaxLength);
		policy.insert(QStringLiteral("naming"), naming);
	}

	// 排座位策略
	{
		QJsonObject seat;
		seat.insert(QStringLiteral("layout"), seatLayout);
		seat.insert(QStringLiteral("order"), seatOrder);
		policy.insert(QStringLiteral("seat"), seat);
	}

	// 准考证号规则
	QJsonObject idRule;
	idRule.insert(QStringLiteral("template"), idTemplate);
	{
		QJsonArray chars;

		for (const QString &source : idCharSources) {
			QJsonObject item;
			item.insert(QStringLiteral("source"), source);
			chars.append(item);
		}

		idRule.insert(QStringLiteral("charGroups"), chars);
		idRule.insert(QStringLiteral("scope"), QStringLiteral("region"));
		idRule.insert(QStringLiteral("order"), QStringLiteral("row"));
	}

	QJsonObject config;
	config.insert(QStringLiteral("version"), QStringLiteral("1.0"));
	config.insert(QStringLiteral("title"), title);
	config.insert(QStringLiteral("examTime"), examTime);
	config.insert(QStringLiteral("idRule"), idRule);
	config.insert(QStringLiteral("generatePolicy"), policy);

	if (! writeText(configPath(), QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Indented)), error))
		return false;

	return true;
}

void AdmissionProject::migrateLegacy(QStringList *log) {
	const QDir rootDir(root());

	if (! rootDir.exists())
		return;

	// 模板已经内置，旧版生成的 template.tex 直接删掉（里面是旧版布局，留着会误导）。
	if (QFileInfo::exists(root() + QStringLiteral("template.tex"))) {
		QFile::remove(root() + QStringLiteral("template.tex"));

		if (log)
			*log << QStringLiteral("removed admission/template.tex (the template is built in now)");
	}

	// regions/ 已经存在就说明迁过了（别再动一次：admission/ 下的 contest-notes.md
	// 之类会再被当成一个「赛区」，凭空多出一个目录）。
	if (QDir(regionsRoot()).exists())
		return;

	const QStringList csvs = rootDir.entryList({QStringLiteral("*.csv")}, QDir::Files, QDir::Name);
	const QStringList mds = rootDir.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);

	if (csvs.isEmpty() && mds.isEmpty())
		return;

	QDir().mkpath(regionsRoot());

	for (const QString &name : csvs) {
		const QString base = QFileInfo(name).completeBaseName();
		const QString region = (base.compare(QStringLiteral("tickets"), Qt::CaseInsensitive) == 0) ? QString() : base;
		moveFile(root() + name, contestantsPath(region), log);
	}

	for (const QString &name : mds) {
		const QString base = QFileInfo(name).completeBaseName();

		// contest-notes.md 是整场的「比赛注意」，不是赛区（老版把它放在 admission/ 下）。
		if (base.compare(QStringLiteral("contest-notes"), Qt::CaseInsensitive) == 0)
			continue;

		const QString region = (base.compare(QStringLiteral("tickets"), Qt::CaseInsensitive) == 0) ? QString() : base;
		QString text;

		if (! readText(root() + name, text))
			continue;

		// 旧版把考点 / 考场 / 测试时间写在 .md 开头：测试时间搬进 config.json，
		// 考场与考点本来就应该写在名单（contestants.csv）里。
		QMap<QString, QString> info;
		const QString body = splitInfo(text, info);
		const QString notes = section(body, QStringLiteral("注意事项"), true);
		const QString general = section(body, QStringLiteral("比赛注意"), false);

		if (! general.isEmpty() && ! QFileInfo::exists(contestNotesPath()))
			writeText(contestNotesPath(), composeNotes(general), nullptr);

		// 旧版的测试时间 / 考场 / 考点都不再是赛区级的：测试时间搬进 config.json（若还没有），
		// 考场与考点本来就应该写在名单（contestants.csv）里。
		const QString legacyTime = info.value(QStringLiteral("测试时间")).trimmed();
		QString configText;

		if (! legacyTime.isEmpty() && readText(configPath(), configText)) {
			QJsonObject config = QJsonDocument::fromJson(configText.toUtf8()).object();

			if (config.value(QStringLiteral("examTime")).toString().isEmpty()) {
				config.insert(QStringLiteral("examTime"), legacyTime);
				writeText(configPath(),
				          QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Indented)), nullptr);
			}
		}

		writeText(notesPath(region), composeNotes(notes), nullptr);
		QFile::remove(root() + name);

		if (log)
			*log << QStringLiteral("moved %1 -> regions/%2/notes.md").arg(name, region.isEmpty() ? QStringLiteral(".") : region);

		if (! QFileInfo::exists(contestantsPath(region)))
			AdmissionCsv::writeTemplate(contestantsPath(region), AdmissionTable::builtinColumns(), nullptr);
	}
}
