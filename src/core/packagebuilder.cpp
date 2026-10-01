/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "packagebuilder.h"
//
#include "base/LemonUtils.hpp"
#include "base/settings.h"
#include "core/contest.h"
#include "core/contestant.h"
#include "core/task.h"
//
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>
#include <QtCore/private/qzipwriter_p.h>
#include <algorithm>
#include <memory>
#ifdef LEMON_HAVE_ZLIB
#include <zlib.h>
#endif

namespace {
	// ------------------------------------------------------------------
	// CRC32 / deflate
	// ------------------------------------------------------------------
	const quint32 *crcTable() {
		static quint32 table[256];
		static bool ready = false;

		if (! ready) {
			for (quint32 i = 0; i < 256; i++) {
				quint32 c = i;

				for (int k = 0; k < 8; k++)
					c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);

				table[i] = c;
			}

			ready = true;
		}

		return table;
	}

	/// 标准 CRC32（含首尾取反），zip 里记录的就是它。
	quint32 crc32Of(const QByteArray &data) {
		const quint32 *table = crcTable();
		quint32 crc = 0xFFFFFFFFu;

		for (const char ch : data)
			crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFFu] ^ (crc >> 8);

		return crc ^ 0xFFFFFFFFu;
	}

	/// ZipCrypto 的密钥更新用的 CRC32：不取反，只按表推进。
	quint32 crc32Step(quint32 crc, quint32 value) {
		return crcTable()[(crc ^ value) & 0xFFu] ^ (crc >> 8);
	}

	/// 原始 deflate 流（不带头尾）。没有 zlib 时返回空，调用方会退化成不压缩（Store）。
	QByteArray rawDeflate(const QByteArray &data) {
#ifdef LEMON_HAVE_ZLIB
		if (data.isEmpty())
			return {};

		z_stream stream{};
		stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
		stream.avail_in = uInt(data.size());

		if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
			return {};

		QByteArray out;
		char buffer[16384];

		while (true) {
			stream.next_out = reinterpret_cast<Bytef *>(buffer);
			stream.avail_out = sizeof(buffer);
			const int status = deflate(&stream, Z_FINISH);

			if (status != Z_OK && status != Z_STREAM_END) {
				deflateEnd(&stream);
				return {};
			}

			out.append(buffer, int(sizeof(buffer)) - int(stream.avail_out));

			if (status == Z_STREAM_END)
				break;
		}

		deflateEnd(&stream);
		return out;
#else
		Q_UNUSED(data);
		return {};
#endif
	}

	// ------------------------------------------------------------------
	// ZIP 写入器：不加密走 Qt 的 QZipWriter，加密自己写（QZipWriter 不支持加密）
	// ------------------------------------------------------------------
	class ArchiveWriter {
	  public:
		virtual ~ArchiveWriter() = default;
		virtual void addDirectory(const QString &path) = 0;
		virtual void addFile(const QString &path, const QByteArray &data) = 0;
		virtual bool finish(QString *error) = 0;
	};

	class QtArchiveWriter : public ArchiveWriter {
	  public:
		explicit QtArchiveWriter(QIODevice *device) : writer(device) {}

		void addDirectory(const QString &path) override { writer.addDirectory(path); }

		void addFile(const QString &path, const QByteArray &data) override { writer.addFile(path, data); }

		bool finish(QString *error) override {
			writer.close();

			if (writer.status() != QZipWriter::NoError) {
				*error = QCoreApplication::translate("PackageBuilder", "Cannot write the archive");
				return false;
			}

			return true;
		}

	  private:
		QZipWriter writer;
	};

	/// 传统 PKWARE 加密（ZipCrypto）：文件名照旧，文件内容加密，压缩用 deflate。
	class CryptoArchiveWriter : public ArchiveWriter {
	  public:
		struct Entry {
			QByteArray name;
			quint32 crc{};
			quint32 compressedSize{};
			quint32 uncompressedSize{};
			quint32 offset{};
			quint16 method{};
			quint16 flags{};
			bool isDirectory{false};
		};

		CryptoArchiveWriter(QIODevice *device, const QByteArray &password)
		    : out(device), password(password), dosTime(dosTimeStamp()), dosDate(dosDateStamp()) {}

		void addDirectory(const QString &path) override {
			QByteArray name = path.toUtf8();

			if (! name.endsWith('/'))
				name.append('/');

			// 目录条目不加密，只记一个以 / 结尾的空条目
			writeLocalHeader(name, 0, 0, 0, {}, 0, 0x0800);

			Entry entry;
			entry.name = name;
			entry.flags = 0x0800;
			entry.offset = lastOffset;
			entry.isDirectory = true;
			entries.append(entry);
		}

		void addFile(const QString &path, const QByteArray &data) override {
			const QByteArray name = path.toUtf8();
			const quint32 crc = crc32Of(data);
			QByteArray payload = rawDeflate(data);
			quint16 method = 8;

			// 压不小就直接存原始数据
			if (payload.isEmpty() || payload.size() >= data.size()) {
				payload = data;
				method = 0;
			}

			// 12 字节加密头：前 11 字节随机，最后一字节是 CRC 高位，解压时用它校验密码
			QByteArray header(12, '\0');

			for (int i = 0; i < 11; i++)
				header[i] = char(QRandomGenerator::global()->bounded(256));

			header[11] = char((crc >> 24) & 0xFFu);

			ZipCrypto crypto(password);
			// 注意：必须严格先加密 12 字节头、再加密内容（密钥流是连续的）。
			// 不能写成 crypto.encrypt(header) + crypto.encrypt(payload)：两个操作数的求值顺序未定义，
			// 一旦先算后面那个，头部就会用错密钥流，解压时会被判成「密码错误」。
			QByteArray encrypted = crypto.encrypt(header);
			encrypted.append(crypto.encrypt(payload));

			writeLocalHeader(name, crc, quint32(encrypted.size()), quint32(data.size()), encrypted, method,
			                 entryFlags());

			Entry entry;
			entry.name = name;
			entry.crc = crc;
			entry.compressedSize = quint32(encrypted.size());
			entry.uncompressedSize = quint32(data.size());
			entry.method = method;
			entry.flags = entryFlags();
			entry.offset = lastOffset;
			entries.append(entry);
		}

		bool finish(QString *error) override {
			const quint32 centralStart = quint32(out->pos());

			for (const Entry &entry : entries) {
				writeU32(0x02014b50);
				writeU16(20); // version made by
				writeU16(20); // version needed
				writeU16(entry.flags);
				writeU16(entry.method);
				writeU16(dosTime);
				writeU16(dosDate);
				writeU32(entry.crc);
				writeU32(entry.compressedSize);
				writeU32(entry.uncompressedSize);
				writeU16(quint16(entry.name.size()));
				writeU16(0); // extra
				writeU16(0); // comment
				writeU16(0); // disk
				writeU16(0); // internal attributes
				writeU32(entry.isDirectory ? 0x10u : 0x0u);
				writeU32(entry.offset);
				out->write(entry.name);
			}

			const quint32 centralSize = quint32(out->pos()) - centralStart;
			writeU32(0x06054b50);
			writeU16(0);
			writeU16(0);
			writeU16(quint16(entries.size()));
			writeU16(quint16(entries.size()));
			writeU32(centralSize);
			writeU32(centralStart);
			writeU16(0); // comment length

			if (auto *file = qobject_cast<QFileDevice *>(out)) {
				if (file->error() != QFileDevice::NoError) {
					*error = file->errorString();
					return false;
				}
			}

			return true;
		}

	  private:
		/// 加密 + UTF-8 文件名
		static quint16 entryFlags() { return 0x0001 | 0x0800; }

		static quint16 dosTimeStamp() {
			const QTime now = QTime::currentTime();
			return quint16((now.hour() << 11) | (now.minute() << 5) | (now.second() / 2));
		}

		static quint16 dosDateStamp() {
			const QDate now = QDate::currentDate();
			return quint16(((now.year() - 1980) << 9) | (now.month() << 5) | now.day());
		}

		void writeU16(quint16 value) {
			const char bytes[2] = {char(value & 0xFFu), char((value >> 8) & 0xFFu)};
			out->write(bytes, 2);
		}

		void writeU32(quint32 value) {
			const char bytes[4] = {char(value & 0xFFu), char((value >> 8) & 0xFFu), char((value >> 16) & 0xFFu),
			                       char((value >> 24) & 0xFFu)};
			out->write(bytes, 4);
		}

		void writeLocalHeader(const QByteArray &name, quint32 crc, quint32 compressedSize,
		                      quint32 uncompressedSize, const QByteArray &body, quint16 method,
		                      quint16 flags) {
			lastOffset = quint32(out->pos());
			writeU32(0x04034b50);
			writeU16(20); // version needed
			writeU16(flags);
			writeU16(method);
			writeU16(dosTime);
			writeU16(dosDate);
			writeU32(crc);
			writeU32(compressedSize);
			writeU32(uncompressedSize);
			writeU16(quint16(name.size()));
			writeU16(0); // extra
			out->write(name);
			out->write(body);
		}

		/// ZipCrypto：三个 32 位密钥 + 一个字节流密码。
		class ZipCrypto {
		  public:
			explicit ZipCrypto(const QByteArray &password) {
				for (const char ch : password)
					update(static_cast<unsigned char>(ch));
			}

			QByteArray encrypt(const QByteArray &data) {
				QByteArray out = data;

				for (int i = 0; i < out.size(); i++) {
					const unsigned char plain = static_cast<unsigned char>(out[i]);
					out[i] = char(plain ^ decryptByte());
					update(plain);
				}

				return out;
			}

		  private:
			void update(unsigned char value) {
				key0 = crc32Step(key0, value);
				key1 = (key1 + (key0 & 0xFFu)) * 134775813u + 1u;
				key2 = crc32Step(key2, (key1 >> 24) & 0xFFu);
			}

			unsigned char decryptByte() const {
				const quint32 temp = (key2 | 2u) & 0xFFFFu;
				return static_cast<unsigned char>(((temp * (temp ^ 1u)) >> 8) & 0xFFu);
			}

			quint32 key0{0x12345678};
			quint32 key1{0x23456789};
			quint32 key2{0x34567890};
		};

		QIODevice *out;
		QByteArray password;
		quint16 dosTime;
		quint16 dosDate;
		quint32 lastOffset{};
		QList<Entry> entries;
	};

	std::unique_ptr<ArchiveWriter> makeWriter(QIODevice *device, const QByteArray &password) {
		if (password.isEmpty())
			return std::make_unique<QtArchiveWriter>(device);

		return std::make_unique<CryptoArchiveWriter>(device, password);
	}

	/// 把一批条目写进压缩包（内容全靠 sourcePath 现读）。
	bool writeItems(ArchiveWriter *writer, const QList<PackageBuilder::Item> &items, QString *error,
	                int *fileCount, int *dirCount) {
		for (const PackageBuilder::Item &item : items) {
			if (item.isDirectory) {
				writer->addDirectory(item.archivePath);
				++*dirCount;
				continue;
			}

			QFile source(item.sourcePath);

			if (! source.open(QIODevice::ReadOnly)) {
				*error = QCoreApplication::translate("PackageBuilder", "Cannot open file %1")
				             .arg(QDir::toNativeSeparators(item.sourcePath));
				return false;
			}

			writer->addFile(item.archivePath, source.readAll());
			source.close();
			++*fileCount;
		}

		return true;
	}
} // namespace

PackageBuilder::PackageBuilder(QObject *parent) : QObject(parent) {}

auto PackageBuilder::kindNames() -> QStringList {
	return {tr("Contestant Directory"), tr("Test Data"), tr("Answers")};
}

auto PackageBuilder::kindAt(int index) -> Kind {
	const QStringList names = kindNames();

	if (index < 0 || index >= names.size())
		return ContestantPackage;

	return static_cast<Kind>(index);
}

void PackageBuilder::setKind(Kind kind) {
	if (packageKind == kind)
		return;

	packageKind = kind;
	clearCache();
}

void PackageBuilder::setDayFile(const QString &fileName) {
	if (dayFile == fileName)
		return;

	dayFile = fileName;

	if (! fileName.isEmpty())
		dayDir = QFileInfo(fileName).absolutePath();

	clearCache();
}

void PackageBuilder::setDayDirectory(const QString &directory) {
	if (dayDir == directory)
		return;

	dayDir = directory;
	clearCache();
}

void PackageBuilder::setContest(Contest *value) {
	if (contest == value)
		return;

	contest = value;
	clearCache();
}

void PackageBuilder::setOutputFile(const QString &fileName) { output = fileName; }

void PackageBuilder::setWrapInFolder(bool value) {
	if (wrap == value)
		return;

	wrap = value;
	clearCache();
}

void PackageBuilder::setNestedZip(bool value) {
	if (nested == value)
		return;

	nested = value;
	clearCache();
}

void PackageBuilder::setOneFolderPerTask(bool value) {
	if (perTask == value)
		return;

	perTask = value;
	clearCache();
}

void PackageBuilder::setKeepStructure(bool value) {
	if (structure == value)
		return;

	structure = value;
	clearCache();
}

void PackageBuilder::setIncludeSamples(bool value) {
	if (samples == value)
		return;

	samples = value;

	// 样例数据必须保留结构，否则 data/ 与 down/ 里的同名文件会撞在一起。
	if (value && ! structure) {
		structure = true;
	}

	clearCache();
}

void PackageBuilder::clearCache() { cache = Cache(); }

auto PackageBuilder::innerArchiveName() const -> QString {
	// 选手目录：整包内容全放进一个固定叫 down.zip 的内层包。
	if (packageKind == ContestantPackage)
		return QStringLiteral("down.zip");

	// 测试数据 / 选手代码（没有赛区时）：内层包与比赛日文件同名。
	return Lemon::common::FileNameSafePart(dayName(), QStringLiteral("day")) + QStringLiteral(".zip");
}

void PackageBuilder::setPassword(const QString &value) { pass = value; }

auto PackageBuilder::dayRoot() const -> QString {
	if (! dayDir.isEmpty())
		return dayDir;

	if (! dayFile.isEmpty())
		return QFileInfo(dayFile).absolutePath();

	return QDir::currentPath();
}

auto PackageBuilder::dayName() const -> QString {
	if (! dayFile.isEmpty())
		return QFileInfo(dayFile).completeBaseName();

	return QFileInfo(dayRoot()).fileName();
}

auto PackageBuilder::defaultOutputFile() const -> QString {
	// 三个包各有固定名字，免得互相撞名：
	// 选手目录 = <比赛日>.zip，测试数据 = evaldata.zip，选手代码 = answers.zip。
	QString name;

	switch (packageKind) {
		case TestDataPackage:
			name = QStringLiteral("evaldata");
			break;

		case AnswersPackage:
			name = QStringLiteral("answers");
			break;

		case ContestantPackage:
			name = Lemon::common::FileNameSafePart(dayName(), QStringLiteral("day"));
			break;
	}

	const QString dir = dayFile.isEmpty() ? dayRoot() : QFileInfo(dayFile).absolutePath();

	return QDir(dir).absoluteFilePath(QStringLiteral("export") + QDir::separator() + name +
	                                  QStringLiteral(".zip"));
}

// down 目录里的文件：只取本层，不递归子目录。
auto PackageBuilder::collectDownFiles(const QString &taskName) -> QList<QPair<QString, QString>> {
	QList<QPair<QString, QString>> files;

	if (taskName.isEmpty())
		return files;

	const QString down = QDir(dayRoot()).absoluteFilePath(Settings::dataPath() + taskName + QDir::separator() +
	                                                      QStringLiteral("down"));
	const QDir downDir(down);

	if (! downDir.exists()) {
		emit logMessage(tr("No down folder for task %1: %2").arg(taskName, QDir::toNativeSeparators(down)));
		return files;
	}

	const QFileInfoList found =
	    downDir.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);

	for (const QFileInfo &info : found)
		files.append({info.fileName(), info.absoluteFilePath()});

	return files;
}

// 选手目录：每道题一个子目录（放 down 里的文件），题面 PDF 放根目录。
auto PackageBuilder::collectContestantPackage(QList<Item> &items) -> bool {
	if (! contest) {
		error = tr("No contest");
		return false;
	}

	// 选「套一层目录」时，所有内容都放进与比赛日文件同名的目录里。
	const QString prefix =
	    wrap ? Lemon::common::FileNameSafePart(dayName(), QStringLiteral("day")) + QChar('/') : QString();

	if (wrap) {
		Item folder;
		folder.archivePath = prefix.left(prefix.size() - 1);
		folder.isDirectory = true;
		items.append(folder);
	}

	// 题面 PDF：整场只有一份，放压缩包根目录。
	const QString pdf =
	    QDir(dayRoot()).absoluteFilePath(Settings::statementPath() + QStringLiteral("statement.pdf"));

	if (QFileInfo::exists(pdf)) {
		Item item;
		item.archivePath = prefix + QStringLiteral("statement.pdf");
		item.sourcePath = pdf;
		items.append(item);
	} else {
		// 缺题面照样能打包，只是提醒一句（界面会弹窗再确认一次）。
		emit logMessage(tr("Statement PDF not found: %1").arg(QDir::toNativeSeparators(pdf)));
	}

	const QList<Task *> taskList = contest->getTaskList();

	for (auto *task : taskList) {
		const QString taskName = task->getDirectoryName();
		const QString folder = prefix + Lemon::common::FileNameSafePart(taskName, QStringLiteral("task"));
		const QList<QPair<QString, QString>> files = collectDownFiles(taskName);

		// 目录条目：就算 down 里没文件也建出来，包结构保持一致。
		Item dir;
		dir.archivePath = folder;
		dir.isDirectory = true;
		items.append(dir);

		for (const auto &file : files) {
			Item item;
			item.archivePath = folder + QChar('/') + file.first;
			item.sourcePath = file.second;
			items.append(item);
		}

		emit logMessage(tr("%1: %2 file(s)").arg(folder).arg(files.size()));
	}

	return true;
}

// 测试数据包：一道题要打包的文件（相对题目录的路径, 磁盘路径）。
// data/ 与 graders/ 总是带上；down/（样例数据）只有勾了才带上，且一定保留结构。
auto PackageBuilder::collectTestDataFiles(const Task *task, bool quiet) -> QList<QPair<QString, QString>> {
	QList<QPair<QString, QString>> files;

	if (! task)
		return files;

	const QString taskName = task->getDirectoryName();
	const QString taskRoot =
	    QDir(dayRoot()).absoluteFilePath(Settings::dataPath() + taskName + QDir::separator());

	// 保留结构时原样搬过去（data/xxx）；不保留时都铺到题目录下（xxx）。
	QList<QPair<QString, QString>> parts = {
	    {QStringLiteral("data"), structure ? QStringLiteral("data") : QString()},
	    {QStringLiteral("graders"), structure ? QStringLiteral("graders") : QString()},
	};

	if (samples)
		parts.append({QStringLiteral("down"), QStringLiteral("down")});

	for (const auto &part : parts) {
		const QDir dir(taskRoot + part.first);

		if (! dir.exists()) {
			// down/ 没有很正常，不打扰；data/、graders/ 缺了提醒一句。
			if (! quiet && part.first != QLatin1String("down"))
				emit logMessage(tr("No %1 folder for task %2").arg(part.first, taskName));

			continue;
		}

		QDirIterator iterator(dir.path(), QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
		                      QDirIterator::Subdirectories);

		while (iterator.hasNext()) {
			iterator.next();
			const QString name = dir.relativeFilePath(iterator.filePath());
			// 保留结构时连 data/ 里的子目录一起照搬；不保留时只留文件名（撞名会被跳过并记日志）。
			const QString relative = part.second.isEmpty()
			                             ? QFileInfo(name).fileName()
			                             : part.second + QChar('/') + name;
			files.append({relative, iterator.filePath()});
		}
	}

	std::sort(files.begin(), files.end(),
	          [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
		          return a.first < b.first;
	          });

	return files;
}

// 各题的文件是否重名：重名的话「各题单独一个目录」就不能关。
auto PackageBuilder::hasDuplicateTaskFiles() -> bool {
	if (cache.duplicatesValid)
		return cache.duplicates;

	bool duplicated = false;

	if (contest) {
		QSet<QString> seen;

		for (auto *task : contest->getTaskList()) {
			for (const auto &file : collectTestDataFiles(task, true)) {
				const QString key = structure ? file.first : QFileInfo(file.first).fileName();

				if (seen.contains(key)) {
					duplicated = true;
					break;
				}

				seen.insert(key);
			}

			if (duplicated)
				break;
		}
	}

	cache.duplicates = duplicated;
	cache.duplicatesValid = true;
	return duplicated;
}

// 测试数据：每题的 data/ / graders/（可选 down/），按开关决定是否保留结构与是否分题目录。
auto PackageBuilder::collectTestDataPackage(QList<Item> &items) -> bool {
	if (! contest) {
		error = tr("No contest");
		return false;
	}

	const QString prefix =
	    wrap ? Lemon::common::FileNameSafePart(dayName(), QStringLiteral("day")) + QChar('/') : QString();

	if (wrap) {
		Item folder;
		folder.archivePath = prefix.left(prefix.size() - 1);
		folder.isDirectory = true;
		items.append(folder);
	}

	QSet<QString> dirs;
	QSet<QString> placed;
	auto addDir = [&](const QString &path) {
		if (path.isEmpty() || dirs.contains(path))
			return;

		dirs.insert(path);
		Item item;
		item.archivePath = path;
		item.isDirectory = true;
		items.append(item);
	};

	const QList<Task *> taskList = contest->getTaskList();

	for (auto *task : taskList) {
		const QString taskName = task->getDirectoryName();
		// 分摊到公共目录时（关掉「每道题单独一个目录」），文件直接放在套层目录/根下。
		const QString base = perTask ? prefix + Lemon::common::FileNameSafePart(taskName, QStringLiteral("task")) +
		                                  QChar('/')
		                            : prefix;

		if (perTask)
			addDir(base.left(base.size() - 1));

		const QList<QPair<QString, QString>> files = collectTestDataFiles(task, false);
		int count = 0;

		for (const auto &file : files) {
			const QString archivePath = base + file.first;
			const int slash = archivePath.lastIndexOf(QChar('/'));

			if (placed.contains(archivePath)) {
				emit logMessage(tr("Duplicate file name skipped: %1").arg(archivePath));
				continue;
			}

			placed.insert(archivePath);

			if (slash > 0)
				addDir(archivePath.left(slash));

			Item item;
			item.archivePath = archivePath;
			item.sourcePath = file.second;
			items.append(item);
			++count;
		}

		emit logMessage(tr("%1: %2 file(s)").arg(perTask ? QString(base).chopped(1) : taskName).arg(count));
	}

	return true;
}

// 选手代码：每位选手一个目录，里面是他全部题目的源代码。
// 启用赛区时按赛区分组，可以同时给每个赛区建同名文件夹、同名内层压缩包（外层固定 answers.zip）；
// 套层打开时，赛区这一层放在「与比赛日同名」的目录里：answers.zip/<比赛日>/<赛区>/<选手>/…
// 没启用赛区（或选手没填赛区）时，整体就当成一组，与其它包一样：套层 = <比赛日>/。
auto PackageBuilder::collectAnswersPackage(Plan &plan) -> bool {
	if (! contest) {
		error = tr("No contest");
		return false;
	}

	const bool byRegion = contest->getRegionEnabled();
	const QString dayFolder = Lemon::common::FileNameSafePart(dayName(), QStringLiteral("day"));

	// 组的 key 就是赛区名；未启用赛区时整场算一组（key 为空）。
	QMap<QString, QList<Contestant *>> groups;

	for (auto *contestant : contest->getContestantList())
		groups[byRegion ? contestant->getRegion().trimmed() : QString()].append(contestant);

	if (groups.isEmpty()) {
		error = tr("No contestant in current contest");
		return false;
	}

	// 先按组收集条目（路径相对该组）。
	QMap<QString, QList<Item>> grouped;

	for (auto it = groups.begin(); it != groups.end(); ++it) {
		QList<Item> groupItems;

		for (auto *contestant : it.value()) {
			const QString folder = Lemon::common::FileNameSafePart(contestant->getContestantName(),
			                                                      QStringLiteral("contestant"));
			const QString source = QDir(dayRoot()).absoluteFilePath(contestant->getSourceFolder());

			Item dir;
			dir.archivePath = folder;
			dir.isDirectory = true;
			groupItems.append(dir);

			const QDir sourceDir(source);
			QDirIterator iterator(source, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
			                      QDirIterator::Subdirectories);

			while (iterator.hasNext()) {
				iterator.next();
				Item file;
				file.archivePath = folder + QChar('/') + sourceDir.relativeFilePath(iterator.filePath());
				file.sourcePath = iterator.filePath();
				groupItems.append(file);
			}
		}

		grouped.insert(it.key(), groupItems);
	}

	// 没有赛区：跟其它包一样，整场内容（套层打开时套一层 <比赛日>/）。
	if (! byRegion) {
		QList<Item> content = grouped.value(QString());

		for (auto &item : content) {
			if (wrap)
				item.archivePath = dayFolder + QChar('/') + item.archivePath;
		}

		if (wrap) {
			Item dir;
			dir.archivePath = dayFolder;
			dir.isDirectory = true;
			content.prepend(dir);
		}

		if (nested)
			plan.archives.append({innerArchiveName(), content});
		else
			plan.items = content;

		emit logMessage(tr("%1: %2 file(s)").arg(dayFolder).arg(content.size()));
		return true;
	}

	// 启用赛区：套层时先建「与比赛日同名」的目录，再在每个赛区下面建同名目录。
	const QString dayPrefix = wrap ? dayFolder + QChar('/') : QString();

	if (wrap) {
		Item dir;
		dir.archivePath = dayFolder;
		dir.isDirectory = true;
		plan.items.append(dir);
	}

	QSet<QString> dirs;
	QSet<QString> placed;

	for (auto it = grouped.begin(); it != grouped.end(); ++it) {
		const QString region = it.key();
		// 没填赛区的选手不额外建一层目录，直接放在 <比赛日>/ 下。
		const QString label =
		    region.isEmpty() ? dayFolder : Lemon::common::FileNameSafePart(region, QStringLiteral("region"));
		const QString base = dayPrefix + (region.isEmpty() ? QString() : label + QChar('/'));
		const QList<Item> groupItems = it.value();
		int count = 0;

		// 赛区同名文件夹是强制的：启用赛区时每个赛区一定单独一层目录。
		if (! region.isEmpty() && ! dirs.contains(dayPrefix + label)) {
			const QString folderPath = dayPrefix + label;
			dirs.insert(folderPath);
			Item dir;
			dir.archivePath = folderPath;
			dir.isDirectory = true;
			plan.items.append(dir);
		}

		for (const Item &item : groupItems) {
			Item copy = item;
			copy.archivePath = base + item.archivePath;

			if (placed.contains(copy.archivePath)) {
				emit logMessage(tr("Duplicate file name skipped: %1").arg(copy.archivePath));
				continue;
			}

			placed.insert(copy.archivePath);
			plan.items.append(copy);
			++count;
		}

		// 内层压缩包：每个赛区一个与赛区同名的 zip（套层时放在 <比赛日>/ 下）。
		if (nested) {
			plan.archives.append({dayPrefix + label + QStringLiteral(".zip"), groupItems});
			count += groupItems.size();
		}

		emit logMessage(tr("%1: %2 file(s)").arg(label).arg(count));
	}

	return true;
}

auto PackageBuilder::collectItems() -> Plan {
	error.clear();

	if (cache.planValid)
		return cache.plan;

	Plan plan;
	QList<Item> content;

	switch (packageKind) {
		case ContestantPackage:
		case TestDataPackage: {
			const bool ok = packageKind == ContestantPackage ? collectContestantPackage(content)
			                                                 : collectTestDataPackage(content);

			if (! ok) {
				plan.items.clear();
				plan.archives.clear();
				return plan;
			}

			// 选了内层压缩包：整包内容都在内层包里（选手目录固定叫 down.zip，
			// 测试数据与比赛日文件同名）；否则内容直接放外层。
			if (nested)
				plan.archives.append({innerArchiveName(), content});
			else
				plan.items = content;

			break;
		}

		case AnswersPackage:
			if (! collectAnswersPackage(plan)) {
				plan.items.clear();
				plan.archives.clear();
				return plan;
			}

			break;
	}

	cache.plan = plan;
	cache.planValid = true;
	return plan;
}

auto PackageBuilder::build() -> bool {
	error.clear();

	// 真正打包前重新读一遍磁盘，避免用的是界面预览时的旧清单。
	clearCache();

	const Plan plan = collectItems();

	if (! error.isEmpty())
		return false;

	if (plan.items.isEmpty() && plan.archives.isEmpty()) {
		error = tr("Nothing to export");
		return false;
	}

	const QString target = output.isEmpty() ? defaultOutputFile() : output;

	if (! QDir().mkpath(QFileInfo(target).absolutePath())) {
		error = tr("Cannot make folder %1").arg(QDir::toNativeSeparators(QFileInfo(target).absolutePath()));
		return false;
	}

	if (QFileInfo::exists(target) && ! QFile::remove(target)) {
		error = tr("Cannot overwrite %1").arg(QDir::toNativeSeparators(target));
		return false;
	}

	emit logMessage(tr("Writing %1").arg(QDir::toNativeSeparators(target)));

	QFile out(target);

	if (! out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		error = tr("Cannot open file %1").arg(QDir::toNativeSeparators(target));
		return false;
	}

	std::unique_ptr<ArchiveWriter> writer = makeWriter(&out, pass.toUtf8());
	int fileCount = 0;
	int dirCount = 0;

	if (! writeItems(writer.get(), plan.items, &error, &fileCount, &dirCount))
		return false;

	// 内层压缩包：每个先在内存里打一份（同样受密码保护），再作为一项放进外层。
	for (const NestedArchive &archive : plan.archives) {
		QBuffer buffer;
		buffer.open(QIODevice::WriteOnly);
		std::unique_ptr<ArchiveWriter> inner = makeWriter(&buffer, pass.toUtf8());
		int innerFiles = 0;
		int innerDirs = 0;

		if (! writeItems(inner.get(), archive.items, &error, &innerFiles, &innerDirs))
			return false;

		if (! inner->finish(&error))
			return false;

		emit logMessage(
		    tr("%1: %2 folder(s), %3 file(s)").arg(archive.name).arg(innerDirs).arg(innerFiles));

		writer->addFile(archive.name, buffer.data());
		++fileCount;
	}

	if (! writer->finish(&error))
		return false;

	out.close();

	if (out.error() != QFileDevice::NoError) {
		error = out.errorString();
		return false;
	}

	emit logMessage(tr("Done: %1 folder(s), %2 file(s)").arg(dirCount).arg(fileCount));
	return true;
}
