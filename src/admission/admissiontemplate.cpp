/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "admissiontemplate.h"
//
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace AdmissionTemplate {
	namespace {
		const QString kLockedBegin = QStringLiteral("% === LOCKED ROWS BEGIN ===");
		const QString kLockedEnd = QStringLiteral("% === LOCKED ROWS END ===");

		QString readText(const QString &path) {
			QFile file(path);

			if (! file.open(QIODevice::ReadOnly))
				return {};

			QString text = QString::fromUtf8(file.readAll());
			text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
			text.replace(QChar('\r'), QChar('\n'));

			if (text.startsWith(QChar(0xFEFF)))
				text.remove(0, 1);

			return text;
		}
	} // namespace

	QString root() {
		static const QString cached = [] {
			const QStringList candidates = {
			    QCoreApplication::applicationDirPath() + QStringLiteral("/templates/admission"),
			    QCoreApplication::applicationDirPath() + QStringLiteral("/assets/templates/admission"),
			    QCoreApplication::applicationDirPath() + QStringLiteral("/../templates/admission"),
			    QCoreApplication::applicationDirPath() + QStringLiteral("/../assets/templates/admission"),
			};

			for (const QString &candidate : candidates) {
				const QFileInfo info(candidate);

				if (info.isDir() && QFile::exists(info.absoluteFilePath() + QStringLiteral("/main.tex")))
					return QDir::fromNativeSeparators(info.absoluteFilePath());
			}

			return QString();
		}();

		return cached;
	}

	QString filePath() {
		const QString dir = root();
		return dir.isEmpty() ? QString() : dir + QStringLiteral("/main.tex");
	}

	QString source() {
		const QString path = filePath();
		return path.isEmpty() ? QString() : readText(path);
	}

	QString fontDir() {
		const QString dir = root();

		if (dir.isEmpty())
			return {};

		const QString fonts = QDir::fromNativeSeparators(dir + QStringLiteral("/fonts"));
		return QFileInfo(fonts).isDir() ? fonts : QString();
	}

	QStringList lockedRowLabels() {
		return {QStringLiteral("姓名"), QStringLiteral("准考证号"), QStringLiteral("测试时间"),
		        QStringLiteral("考点"), QStringLiteral("考场"),     QStringLiteral("座位号")};
	}

	bool validate(const QString &text, QString *error) {
		const int begin = text.indexOf(kLockedBegin);
		const int end = text.indexOf(kLockedEnd);

		if (begin < 0 || end < begin) {
			if (error)
				*error = QObject::tr("The template has no locked area markers.");
			return false;
		}

		const QStringList lines = text.mid(begin + kLockedBegin.length(), end - begin - kLockedBegin.length())
		                              .split(QChar('\n'));
		QStringList rows;

		for (const QString &line : lines)
			if (line.contains(QChar('&')) && line.trimmed().endsWith(QStringLiteral("\\\\")))
				rows << line.trimmed();

		const QStringList expected = {QStringLiteral("{{name}}"), QStringLiteral("{{id}}"),
		                              QStringLiteral("{{examTime}}"), QStringLiteral("{{venue}}"),
		                              QStringLiteral("{{room}}"), QStringLiteral("{{seat}}")};

		if (rows.size() != expected.size()) {
			if (error)
				*error = QObject::tr("The locked area must have %1 rows, found %2.")
				             .arg(expected.size())
				             .arg(rows.size());
			return false;
		}

		for (int i = 0; i < expected.size(); ++i) {
			if (! rows.at(i).contains(expected.at(i))) {
				if (error)
					*error = QObject::tr("Row %1 of the locked area must contain %2.")
					             .arg(i + 1)
					             .arg(expected.at(i));
				return false;
			}
		}

		const QStringList required = {QStringLiteral("{{title}}"),       QStringLiteral("{{notes}}"),
		                              QStringLiteral("{{contestNotes}}"), QStringLiteral("{{extraRows}}"),
		                              QStringLiteral("{{photo}}")};

		for (const QString &key : required)
			if (! text.contains(key)) {
				if (error)
					*error = QObject::tr("The template is missing %1.").arg(key);
				return false;
			}

		return true;
	}

	QString render(const QString &text, const QMap<QString, QString> &values, QStringList *unresolved) {
		static const QRegularExpression placeholder(QStringLiteral(R"(\{\{([A-Za-z0-9_\-\.]{1,40})\}\})"));
		QString out;
		QStringList found;
		const QStringList lines = text.split(QChar('\n'));

		// 注释里不替换：注释里写 {{xxx}} 只是给程序看的说明，而且多行替换会把
		// 后面的行挤出注释，直接变成参与编译的真代码。
		for (int i = 0; i < lines.size(); ++i) {
			if (i > 0)
				out += QChar('\n');

			const QString &line = lines.at(i);
			int commentAt = -1;

			for (int position = 0; position < line.length(); ++position) {
				if (line.at(position) == QChar('%') &&
				    (position == 0 || line.at(position - 1) != QChar('\\'))) {
					commentAt = position;
					break;
				}
			}

			QString head = commentAt >= 0 ? line.left(commentAt) : line;
			const QString tail = commentAt >= 0 ? line.mid(commentAt) : QString();

			for (auto it = values.constBegin(); it != values.constEnd(); ++it)
				head.replace(QStringLiteral("{{") + it.key() + QStringLiteral("}}"), it.value());

			if (unresolved && head.contains(QStringLiteral("{{"))) {
				auto match = placeholder.globalMatch(head);

				while (match.hasNext()) {
					const QString key = match.next().captured(1);

					if (! found.contains(key))
						found << key;
				}
			}

			out += head + tail;
		}

		if (unresolved)
			*unresolved += found;

		return out;
	}
} // namespace AdmissionTemplate
