/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/statementdocument.h"
//
#include "base/LemonLog.hpp"
//
#include <QRegularExpression>

#define LEMON_MODULE_NAME "Statement"

namespace {
	const QStringList &knownSectionsList() {
		static const QStringList list = {QStringLiteral("overview_table"), QStringLiteral("submission_filename"),
		                                 QStringLiteral("compile_options"), QStringLiteral("notice")};
		return list;
	}

	const QStringList &editableMetaKeysList() {
		static const QStringList list = {QStringLiteral("title"), QStringLiteral("subtitle"), QStringLiteral("day"),
		                                 QStringLiteral("date")};
		return list;
	}

	const QString &problemMarkerText() { return QStringLiteral("<!-- PROBLEM -->"); }

	const QRegularExpression &sectionPattern() {
		static const QRegularExpression re(
		    QStringLiteral(R"(<!--\s*SECTION:\s*(?<name>[A-Za-z0-9_\-]+)\s*-->(?<body>.*?)<!--\s*END:\s*\k<name>\s*-->)"),
		    QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
		return re;
	}

	const QRegularExpression &problemMarkerLinePattern() {
		static const QRegularExpression re(QStringLiteral(R"(^\s*<!--\s*PROBLEM\s*-->\s*$)"),
		                                   QRegularExpression::CaseInsensitiveOption);
		return re;
	}

	const QRegularExpression &headingStartPattern() {
		static const QRegularExpression re(QStringLiteral(R"(^#(?:\s|$))"));
		return re;
	}

	const QRegularExpression &emptyHeadingPattern() {
		static const QRegularExpression re(QStringLiteral(R"(^#\s*$)"));
		return re;
	}

	const QRegularExpression &headingWithEnglishPattern() {
		static const QRegularExpression re(QStringLiteral(R"(^#\s+(?<title>.+?)\s*[（(](?<eng>[^（）()]*)[）)]\s*$)"));
		return re;
	}

	const QRegularExpression &headingOnlyPattern() {
		static const QRegularExpression re(QStringLiteral(R"(^#\s+(?<title>.+)$)"));
		return re;
	}

	QString normalizeNewlines(const QString &text) {
		QString result = text;
		result.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		result.replace(QChar('\r'), QChar('\n'));

		if (! result.isEmpty() && result.at(0) == QChar(0xfeff))
			result.remove(0, 1);

		return result;
	}

	QString escapeMetaValue(const QString &value) {
		QString result = value;
		result.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
		result.replace(QStringLiteral("\""), QStringLiteral("\\\""));
		return result;
	}

	/// 只去掉首尾的换行符（保留空格），与 statement-editor 的 `strip("\n")` 一致。
	QString stripNewlines(const QString &text) {
		int begin = 0;
		int end = text.length();

		while (begin < end && text.at(begin) == QChar('\n'))
			begin++;

		while (end > begin && text.at(end - 1) == QChar('\n'))
			end--;

		return text.mid(begin, end - begin);
	}

	/// 去掉首尾的空行（与 statement-editor 的 `_normalize_block` 一致）。
	QString normalizeBlock(const QString &text) {
		QStringList lines = text.split(QChar('\n'));

		for (QString &line : lines)
			while (! line.isEmpty() && line.at(line.length() - 1).isSpace())
				line.chop(1);

		while (! lines.isEmpty() && lines.first().trimmed().isEmpty())
			lines.removeFirst();

		while (! lines.isEmpty() && lines.last().trimmed().isEmpty())
			lines.removeLast();

		return lines.join(QChar('\n'));
	}

	QString renderSectionBlock(const QString &name, const QString &body) {
		if (body.isEmpty())
			return QStringLiteral("<!-- SECTION: %1 -->\n<!-- END: %1 -->").arg(name);

		return QStringLiteral("<!-- SECTION: %1 -->\n%2\n<!-- END: %1 -->").arg(name, body);
	}

	struct MetaEntry {
		QString key;
		QString value;
		QString raw;
	};

	void parseFrontMatter(const QString &text, QList<MetaEntry> &entries, bool &hasFront, QString &rest) {
		entries.clear();
		hasFront = false;
		rest = text;

		if (! text.startsWith(QStringLiteral("---\n")))
			return;

		const QStringList lines = text.split(QChar('\n'));
		int endIndex = -1;

		for (int i = 1; i < lines.size(); i++)
			if (lines.at(i).trimmed() == QStringLiteral("---")) {
				endIndex = i;
				break;
			}

		if (endIndex < 0)
			return;

		for (int i = 1; i < endIndex; i++) {
			const QString raw = lines.at(i);
			const QString stripped = raw.trimmed();
			MetaEntry entry;
			entry.raw = raw;

			if (stripped.isEmpty() || stripped.startsWith(QChar('#'))) {
				entries.append(entry);
				continue;
			}

			const int colon = raw.indexOf(QChar(':'));

			if (colon >= 0) {
				entry.key = raw.left(colon).trimmed().toLower();
				QString value = raw.mid(colon + 1).trimmed();

				if (value.length() >= 2 &&
				    ((value.startsWith(QChar('"')) && value.endsWith(QChar('"'))) ||
				     (value.startsWith(QChar('\'')) && value.endsWith(QChar('\'')))))
					value = value.mid(1, value.length() - 2);

				entry.value = value;
			}

			entries.append(entry);
		}

		QStringList tail;

		for (int i = endIndex + 1; i < lines.size(); i++)
			tail.append(lines.at(i));

		rest = tail.join(QChar('\n'));
		hasFront = true;
	}

	QString buildFrontMatter(const QList<MetaEntry> &entries, const QMap<QString, QString> &values) {
		QStringList kept;
		QSet<QString> overwritten;

		for (const MetaEntry &entry : entries) {
			if (entry.key.isEmpty()) {
				kept.append(entry.raw);
				continue;
			}

			if (StatementDocument::editableMetaKeys().contains(entry.key) && values.contains(entry.key)) {
				const QString newValue = values.value(entry.key).trimmed();

				if (! newValue.isEmpty()) {
					kept.append(QStringLiteral("%1: \"%2\"").arg(entry.key, escapeMetaValue(newValue)));
					overwritten.insert(entry.key);
				}

				continue;
			}

			kept.append(entry.raw);
		}

		for (const QString &key : StatementDocument::editableMetaKeys()) {
			const QString newValue = values.value(key).trimmed();

			if (! newValue.isEmpty() && ! overwritten.contains(key))
				kept.append(QStringLiteral("%1: \"%2\"").arg(key, escapeMetaValue(newValue)));
		}

		return kept.join(QChar('\n'));
	}
} // namespace

const QStringList &StatementDocument::knownSections() { return knownSectionsList(); }

const QStringList &StatementDocument::editableMetaKeys() { return editableMetaKeysList(); }

const QString &StatementDocument::problemMarker() { return problemMarkerText(); }

QStringList StatementDocument::splitLines(const QString &text) { return text.split(QChar('\n')); }

bool StatementDocument::isProblemMarker(const QString &line) {
	return problemMarkerLinePattern().match(line).hasMatch();
}

QString StatementDocument::problemTitleText(const QString &line) {
	QString text = line;
	text.replace(QChar('\r'), QChar(' '));
	text.replace(QChar('\n'), QChar(' '));
	text = text.trimmed();

	while (text.startsWith(QChar('#')))
		text.remove(0, 1);

	return text.trimmed();
}

bool StatementDocument::matchProblemHeading(const QString &line, QString &title, QString &english) {
	title.clear();
	english.clear();

	if (! headingStartPattern().match(line).hasMatch())
		return false;

	if (emptyHeadingPattern().match(line).hasMatch())
		return true;

	const QRegularExpressionMatch matched = headingWithEnglishPattern().match(line);

	if (matched.hasMatch()) {
		title = matched.captured(QStringLiteral("title")).trimmed();
		english = matched.captured(QStringLiteral("eng")).trimmed();
		return true;
	}

	const QRegularExpressionMatch fallback = headingOnlyPattern().match(line);

	if (fallback.hasMatch()) {
		title = fallback.captured(QStringLiteral("title")).trimmed();
		english.clear();
		return true;
	}

	return false;
}

void StatementDocument::splitProblemArea(const QString &text, QStringList &head, QList<StatementProblem> &problems) {
	const QStringList lines = splitLines(text);
	head.clear();
	problems.clear();
	int index = 0;
	QString title;
	QString english;

	while (index < lines.size() && ! matchProblemHeading(lines.at(index), title, english)) {
		head.append(lines.at(index));
		index++;
	}

	while (index < lines.size()) {
		if (! matchProblemHeading(lines.at(index), title, english)) {
			head.append(lines.at(index));
			index++;
			continue;
		}

		StatementProblem problem;
		problem.title = title;
		problem.english = english;
		index++;

		while (index < lines.size() && ! matchProblemHeading(lines.at(index), title, english)) {
			problem.body.append(lines.at(index));
			index++;
		}

		problems.append(problem);
	}
}

QString StatementDocument::serializeProblemArea(const QStringList &head, const QList<StatementProblem> &problems) {
	QStringList chunks;
	const QString headText = normalizeBlock(head.join(QChar('\n')));

	if (! headText.isEmpty())
		chunks.append(headText);

	for (const StatementProblem &problem : problems) {
		QString block = problem.heading();

		if (! problem.body.isEmpty())
			block += QChar('\n') + problem.body.join(QChar('\n'));

		const QString cleaned = normalizeBlock(block);

		if (cleaned.isEmpty())
			chunks.append(problemMarker() + QStringLiteral("\n#"));
		else
			chunks.append(problemMarker() + QChar('\n') + cleaned);
	}

	if (chunks.isEmpty())
		return {};

	QString text = chunks.join(QStringLiteral("\n\n"));

	if (! text.endsWith(QChar('\n')))
		text += QChar('\n');

	return text;
}

QString StatementProblem::heading() const {
	const QString name = title.trimmed();
	const QString eng = english.trimmed();

	if (name.isEmpty() && eng.isEmpty())
		return QStringLiteral("#");

	if (eng.isEmpty())
		return QStringLiteral("# %1").arg(name);

	return QStringLiteral("# %1（%2）").arg(name, eng);
}

StatementDocument StatementDocument::parse(const QString &text) {
	StatementDocument doc;
	const QString normalized = normalizeNewlines(text);

	QList<MetaEntry> entries;
	QString body;
	parseFrontMatter(normalized, entries, doc.hasFront, body);

	for (const MetaEntry &entry : entries) {
		doc.metaEntries.append(qMakePair(entry.key, entry.raw));

		if (! entry.key.isEmpty() && editableMetaKeysList().contains(entry.key) &&
		    ! doc.metaValues.contains(entry.key))
			doc.metaValues.insert(entry.key, entry.value);
	}

	// 按 `<!-- SECTION: -->` 标记切分：区段与其它正文交替保存，未编辑的部分原样保留。
	int position = 0;
	const QRegularExpression &pattern = sectionPattern();
	QRegularExpressionMatchIterator it = pattern.globalMatch(body);

	while (it.hasNext()) {
		const QRegularExpressionMatch matched = it.next();

		if (matched.capturedStart() > position) {
			Item item;
			item.text = body.mid(position, matched.capturedStart() - position);
			doc.items.append(item);
		}

		Item item;
		item.isSection = true;
		item.name = matched.captured(QStringLiteral("name")).trimmed().toLower();
		item.body = matched.captured(QStringLiteral("body")).trimmed();
		doc.items.append(item);
		position = matched.capturedEnd();
	}

	if (position < body.length()) {
		Item item;
		item.text = body.mid(position);
		doc.items.append(item);
	}

	return doc;
}

QString StatementDocument::metadata(const QString &key) const { return metaValues.value(key); }

void StatementDocument::setMetadata(const QString &key, const QString &value) {
	metaValues.insert(key, value);
}

QStringList StatementDocument::sectionNames() const {
	QStringList names;

	for (const Item &item : items)
		if (item.isSection)
			names.append(item.name);

	return names;
}

bool StatementDocument::hasSection(const QString &name) const {
	const QString key = name.toLower();

	for (const Item &item : items)
		if (item.isSection && item.name == key)
			return true;

	return false;
}

QString StatementDocument::sectionBody(const QString &name) const {
	const QString key = name.toLower();

	for (const Item &item : items)
		if (item.isSection && item.name == key)
			return item.body;

	return {};
}

void StatementDocument::setSectionBody(const QString &name, const QString &body) {
	const QString key = name.toLower();
	const QString content = body.trimmed();

	for (int i = 0; i < items.size(); i++) {
		if (! items[i].isSection || items[i].name != key)
			continue;

		if (content.isEmpty()) {
			items.removeAt(i);
			return;
		}

		items[i].body = content;
		return;
	}

	if (! content.isEmpty()) {
		Item item;
		item.isSection = true;
		item.name = key;
		item.body = content;
		items.append(item);
	}
}

int StatementDocument::problemsItem() const {
	int found = -1;

	for (int i = 0; i < items.size(); i++) {
		if (items.at(i).isSection)
			continue;

		bool hit = false;

		for (const QString &line : splitLines(items.at(i).text)) {
			QString title;
			QString english;

			if (isProblemMarker(line) || matchProblemHeading(line, title, english)) {
				hit = true;
				break;
			}
		}

		if (hit)
			found = i;
	}

	return found;
}

QString StatementDocument::problemsText() const {
	const int index = problemsItem();
	return index < 0 ? QString() : items.at(index).text;
}

QString StatementDocument::headText() const {
	QStringList head;
	QList<StatementProblem> problems;
	splitProblemArea(problemsText(), head, problems);
	return head.join(QChar('\n')).trimmed();
}

QList<StatementProblem> StatementDocument::problems() const {
	QStringList head;
	QList<StatementProblem> problems;
	splitProblemArea(problemsText(), head, problems);
	return problems;
}

void StatementDocument::setProblems(const QList<StatementProblem> &problems) {
	QStringList head;
	QList<StatementProblem> existing;
	splitProblemArea(problemsText(), head, existing);
	const QString text = serializeProblemArea(head, problems);

	const int index = problemsItem();

	if (index < 0) {
		Item item;
		item.text = text;
		items.append(item);
		return;
	}

	items[index].text = text;
}

QString StatementDocument::toMarkdown() const {
	QStringList blocks;

	if (hasFront) {
		const QString header = buildFrontMatter(metaEntries, metaValues);
		QString front = QStringLiteral("---\n");

		if (! header.isEmpty())
			front += header + QChar('\n');

		front += QStringLiteral("---");
		blocks.append(front);
	}

	for (const Item &item : items) {
		const QString content = item.isSection ? renderSectionBlock(item.name, item.body) : item.text;
		const QString stripped = stripNewlines(content);

		if (! stripped.trimmed().isEmpty())
			blocks.append(stripped);
	}

	if (blocks.isEmpty())
		return {};

	QString output = blocks.join(QStringLiteral("\n\n"));

	if (! output.endsWith(QChar('\n')))
		output += QChar('\n');

	return output;
}
