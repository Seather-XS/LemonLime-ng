/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "packagebuilder.h"
#include "base/LemonUtils.hpp"
#include "base/settings.h"
#include "core/contest.h"
#include "core/contestant.h"
#include "core/task.h"
#include "core/testcase.h"
#include <QBuffer>
#include <QCryptographicHash>
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

	quint32 crc32Of(const QByteArray &data) {
		const quint32 *table = crcTable();
		quint32 crc = 0xFFFFFFFFu;

		for (const char ch : data)
			crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFFu] ^ (crc >> 8);

		return crc ^ 0xFFFFFFFFu;
	}

	quint32 crc32Step(quint32 crc, quint32 value) {
		return crcTable()[(crc ^ value) & 0xFFu] ^ (crc >> 8);
	}

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

	const unsigned char AES_SBOX[256] = {
	    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

	class Aes256 {
	  public:
		explicit Aes256(const QByteArray &key) { expandKey(key); }

		void encryptBlock(const unsigned char in[16], unsigned char out[16]) const {
			unsigned char state[16];
			std::copy(in, in + 16, state);
			addRoundKey(state, 0);

			for (int round = 1; round < 14; round++) {
				subBytes(state);
				shiftRows(state);
				mixColumns(state);
				addRoundKey(state, round);
			}

			subBytes(state);
			shiftRows(state);
			addRoundKey(state, 14);
			std::copy(state, state + 16, out);
		}

	  private:
		static unsigned char xtime(unsigned char value) {
			return static_cast<unsigned char>((value << 1) ^ ((value & 0x80u) ? 0x1bu : 0x00u));
		}

		static unsigned char multiply(unsigned char left, unsigned char right) {
			unsigned char result = 0;

			while (right != 0) {
				if (right & 1u)
					result ^= left;

				left = xtime(left);
				right = static_cast<unsigned char>(right >> 1);
			}

			return result;
		}

		void expandKey(const QByteArray &key) {
			std::fill(roundKeys, roundKeys + sizeof(roundKeys), 0);
			const int keyBytes = qMin(key.size(), 32);

			for (int i = 0; i < keyBytes; i++)
				roundKeys[i] = static_cast<unsigned char>(key.at(i));

			const int totalWords = 60;
			static const unsigned char rcon[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

			for (int word = 8; word < totalWords; word++) {
				unsigned char temp[4];
				std::copy(roundKeys + (word - 1) * 4, roundKeys + word * 4, temp);

				if (word % 8 == 0) {
					const unsigned char first = temp[0];
					temp[0] = AES_SBOX[temp[1]] ^ rcon[word / 8 - 1];
					temp[1] = AES_SBOX[temp[2]];
					temp[2] = AES_SBOX[temp[3]];
					temp[3] = AES_SBOX[first];
				} else if (word % 8 == 4) {
					for (int i = 0; i < 4; i++)
						temp[i] = AES_SBOX[temp[i]];
				}

				for (int i = 0; i < 4; i++)
					roundKeys[word * 4 + i] = static_cast<unsigned char>(roundKeys[(word - 8) * 4 + i] ^ temp[i]);
			}
		}

		static void subBytes(unsigned char state[16]) {
			for (int i = 0; i < 16; i++)
				state[i] = AES_SBOX[state[i]];
		}

		static void shiftRows(unsigned char state[16]) {
			for (int row = 1; row < 4; row++) {
				unsigned char tmp[4];

				for (int column = 0; column < 4; column++)
					tmp[column] = state[((column + row) % 4) * 4 + row];

				for (int column = 0; column < 4; column++)
					state[column * 4 + row] = tmp[column];
			}
		}

		static void mixColumns(unsigned char state[16]) {
			for (int column = 0; column < 4; column++) {
				unsigned char *s = state + column * 4;
				const unsigned char a0 = s[0];
				const unsigned char a1 = s[1];
				const unsigned char a2 = s[2];
				const unsigned char a3 = s[3];
				s[0] = static_cast<unsigned char>(multiply(a0, 2) ^ multiply(a1, 3) ^ a2 ^ a3);
				s[1] = static_cast<unsigned char>(a0 ^ multiply(a1, 2) ^ multiply(a2, 3) ^ a3);
				s[2] = static_cast<unsigned char>(a0 ^ a1 ^ multiply(a2, 2) ^ multiply(a3, 3));
				s[3] = static_cast<unsigned char>(multiply(a0, 3) ^ a1 ^ a2 ^ multiply(a3, 2));
			}
		}

		void addRoundKey(unsigned char state[16], int round) const {
			for (int i = 0; i < 16; i++)
				state[i] ^= roundKeys[round * 16 + i];
		}

		unsigned char roundKeys[240]{};
	};

	void advanceCounter(unsigned char counter[16]) {
		for (int i = 0; i < 16; i++)
			if (++counter[i] != 0)
				break;
	}

	QByteArray aesCtrCrypt(const QByteArray &key, const QByteArray &data) {
		Aes256 aes(key);
		unsigned char counter[16] = {0};
		counter[0] = 1;
		QByteArray out(data.size(), '\0');

		for (int offset = 0; offset < data.size(); offset += 16) {
			unsigned char stream[16];
			aes.encryptBlock(counter, stream);
			const int blockSize = qMin(16, data.size() - offset);

			for (int i = 0; i < blockSize; i++)
				out[offset + i] = char(static_cast<unsigned char>(data.at(offset + i)) ^ stream[i]);

			advanceCounter(counter);
		}

		return out;
	}

	QByteArray hmacSha1(const QByteArray &key, const QByteArray &data) {
		constexpr int blockSize = 64;
		const QByteArray shortKey =
		    key.size() > blockSize ? QCryptographicHash::hash(key, QCryptographicHash::Sha1) : key;
		QByteArray innerPad(blockSize, '\0');
		QByteArray outerPad(blockSize, '\0');

		for (int i = 0; i < blockSize; i++) {
			const char byte = i < shortKey.size() ? shortKey.at(i) : '\0';
			innerPad[i] = char(static_cast<unsigned char>(byte) ^ 0x36);
			outerPad[i] = char(static_cast<unsigned char>(byte) ^ 0x5c);
		}

		const QByteArray inner = QCryptographicHash::hash(innerPad + data, QCryptographicHash::Sha1);
		return QCryptographicHash::hash(outerPad + inner, QCryptographicHash::Sha1);
	}

	QByteArray pbkdf2Sha1(const QByteArray &password, const QByteArray &salt, int iterations,
	                      int keyLength) {
		QByteArray result;
		quint32 block = 1;

		while (result.size() < keyLength) {
			QByteArray index(4, '\0');
			index[0] = char((block >> 24) & 0xFFu);
			index[1] = char((block >> 16) & 0xFFu);
			index[2] = char((block >> 8) & 0xFFu);
			index[3] = char(block & 0xFFu);
			QByteArray u = hmacSha1(password, salt + index);
			QByteArray accumulated = u;

			for (int round = 1; round < iterations; round++) {
				u = hmacSha1(password, u);

				for (int i = 0; i < accumulated.size(); i++)
					accumulated[i] = char(accumulated.at(i) ^ u.at(i));
			}

			result.append(accumulated);
			++block;
		}

		return result.left(keyLength);
	}

	QByteArray littleEndian16(quint16 value) {
		QByteArray bytes(2, '\0');
		bytes[0] = char(value & 0xFFu);
		bytes[1] = char((value >> 8) & 0xFFu);
		return bytes;
	}

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

	class EncryptedArchiveWriter : public ArchiveWriter {
	  public:
		enum class Method { Aes256, ZipCrypto };

		struct Entry {
			QByteArray name;
			QByteArray extra;
			quint16 versionNeeded{20};
			quint32 crc{};
			quint32 compressedSize{};
			quint32 uncompressedSize{};
			quint32 offset{};
			quint16 method{};
			quint16 flags{};
			bool isDirectory{false};
		};

		EncryptedArchiveWriter(QIODevice *device, const QByteArray &password, Method method)
		    : out(device), password(password), method(method), dosTime(dosTimeStamp()),
		      dosDate(dosDateStamp()) {}

		void addDirectory(const QString &path) override {
			QByteArray name = path.toUtf8();

			if (! name.endsWith('/'))
				name.append('/');

			writeLocalHeader(name, 0, 0, 0, {}, 0, 0x0800, {}, 20);

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

			if (payload.isEmpty() || payload.size() >= data.size()) {
				payload = data;
				method = 0;
			}

			QByteArray body;
			QByteArray extra;
			quint32 storedCrc = crc;
			quint16 version = 20;

			if (this->method == Method::Aes256) {
				QByteArray salt(16, '\0');

				for (char &byte : salt)
					byte = char(QRandomGenerator::global()->bounded(256));

				const QByteArray derived = pbkdf2Sha1(password, salt, 1000, 66);
				const QByteArray aesKey = derived.left(32);
				const QByteArray macKey = derived.mid(32, 32);
				const QByteArray verifier = derived.mid(64, 2);
				const QByteArray encrypted = aesCtrCrypt(aesKey, payload);
				const QByteArray auth = hmacSha1(macKey, encrypted).left(10);

				if (aesCtrCrypt(aesKey, encrypted) != payload || ! hmacSha1(macKey, encrypted).startsWith(auth))
					selfCheckFailed = true;

				body = salt + verifier + encrypted + auth;
				extra = littleEndian16(0x9901) + littleEndian16(7) + littleEndian16(2) + QByteArray("AE", 2) +
				        QByteArray(1, char(0x03)) + littleEndian16(method);
				storedCrc = 0;
				version = 51;
				method = 99; 
			} else {
				QByteArray header(12, '\0');

				for (int i = 0; i < 11; i++)
					header[i] = char(QRandomGenerator::global()->bounded(256));

				header[11] = char((crc >> 24) & 0xFFu);

				ZipCrypto crypto(password);
				QByteArray encrypted = crypto.encrypt(header);
				encrypted.append(crypto.encrypt(payload));
				body = encrypted;

				if (ZipCrypto(password).decrypt(encrypted.left(12)).at(11) != char((crc >> 24) & 0xFFu))
					selfCheckFailed = true;
			}

			writeLocalHeader(name, storedCrc, quint32(body.size()), quint32(data.size()), body, method,
			                 entryFlags(), extra, version);

			Entry entry;
			entry.name = name;
			entry.extra = extra;
			entry.versionNeeded = version;
			entry.crc = storedCrc;
			entry.compressedSize = quint32(body.size());
			entry.uncompressedSize = quint32(data.size());
			entry.method = method;
			entry.flags = entryFlags();
			entry.offset = lastOffset;
			entries.append(entry);
		}

		bool finish(QString *error) override {
			if (selfCheckFailed) {
				*error = QCoreApplication::translate(
				    "PackageBuilder", "The encrypted archive failed its self-check. Please export again.");
				return false;
			}

			const quint32 centralStart = quint32(out->pos());

			for (const Entry &entry : entries) {
				writeU32(0x02014b50);
				writeU16(entry.versionNeeded);
				writeU16(entry.versionNeeded); 
				writeU16(entry.flags);
				writeU16(entry.method);
				writeU16(dosTime);
				writeU16(dosDate);
				writeU32(entry.crc);
				writeU32(entry.compressedSize);
				writeU32(entry.uncompressedSize);
				writeU16(quint16(entry.name.size()));
				writeU16(quint16(entry.extra.size()));
				writeU16(0);
				writeU16(0);
				writeU16(0); 
				writeU32(entry.isDirectory ? 0x10u : 0x0u);
				writeU32(entry.offset);
				out->write(entry.name);
				out->write(entry.extra);
			}

			const quint32 centralSize = quint32(out->pos()) - centralStart;
			writeU32(0x06054b50);
			writeU16(0);
			writeU16(0);
			writeU16(quint16(entries.size()));
			writeU16(quint16(entries.size()));
			writeU32(centralSize);
			writeU32(centralStart);
			writeU16(0); 

			if (auto *file = qobject_cast<QFileDevice *>(out)) {
				if (file->error() != QFileDevice::NoError) {
					*error = file->errorString();
					return false;
				}
			}

			return true;
		}

	  private:
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
		                      quint16 flags, const QByteArray &extra, quint16 version) {
			lastOffset = quint32(out->pos());
			writeU32(0x04034b50);
			writeU16(version); 
			writeU16(flags);
			writeU16(method);
			writeU16(dosTime);
			writeU16(dosDate);
			writeU32(crc);
			writeU32(compressedSize);
			writeU32(uncompressedSize);
			writeU16(quint16(name.size()));
			writeU16(quint16(extra.size()));
			out->write(name);
			out->write(extra);
			out->write(body);
		}

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

			QByteArray decrypt(const QByteArray &data) {
				QByteArray out = data;

				for (int i = 0; i < out.size(); i++) {
					const unsigned char plain = static_cast<unsigned char>(out[i]) ^ decryptByte();
					out[i] = char(plain);
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
		Method method;
		quint16 dosTime;
		quint16 dosDate;
		quint32 lastOffset{};
		QList<Entry> entries;
		bool selfCheckFailed{false};
	};

	std::unique_ptr<ArchiveWriter> makeWriter(QIODevice *device, const QByteArray &password,
	                                          PackageBuilder::EncryptionMethod method) {
		if (password.isEmpty())
			return std::make_unique<QtArchiveWriter>(device);

		const auto writerMethod = method == PackageBuilder::ZipCryptoEncryption
		                              ? EncryptedArchiveWriter::Method::ZipCrypto
		                              : EncryptedArchiveWriter::Method::Aes256;
		return std::make_unique<EncryptedArchiveWriter>(device, password, writerMethod);
	}

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
} 

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

void PackageBuilder::setKind(Kind kind) { packageKind = kind; }

void PackageBuilder::setDayFile(const QString &fileName) {
	dayFile = fileName;

	if (! fileName.isEmpty())
		dayDir = QFileInfo(fileName).absolutePath();
}

void PackageBuilder::setDayDirectory(const QString &directory) { dayDir = directory; }

void PackageBuilder::setContest(Contest *value) {
	if (contest == value)
		return;

	contest = value;
	clearCache();
}

void PackageBuilder::setOutputFile(const QString &fileName) { output = fileName; }

void PackageBuilder::setWrapInFolder(bool value) { wrap = value; }

void PackageBuilder::setNestedZip(bool value) { nested = value; }

void PackageBuilder::setOneFolderPerTask(bool value) { perTask = value; }

void PackageBuilder::setKeepStructure(bool value) { structure = value; }

void PackageBuilder::setIncludeSamples(bool value) {
	samples = value;

	if (value && ! structure)
		structure = true;
}

void PackageBuilder::setStatementFiles(const QStringList &values) {
	statements.clear();

	for (const QString &value : values) {
		const QString name = QFileInfo(value.trimmed()).fileName();

		if (! name.isEmpty() && ! statements.contains(name))
			statements.append(name);
	}
}

auto PackageBuilder::statementFiles() const -> QStringList { return statements; }

void PackageBuilder::setEncryptionMethod(EncryptionMethod value) { encryption = value; }

void PackageBuilder::clearCache() {
	planCache.clear();
	duplicatesCache.clear();
	taskFileCache.clear();
}

auto PackageBuilder::inputKey() const -> QString {
	const QStringList parts{
	    QString::number(int(packageKind)),
	    QString::number(wrap ? 1 : 0),
	    QString::number(nested ? 1 : 0),
	    QString::number(perTask ? 1 : 0),
	    QString::number(structure ? 1 : 0),
	    QString::number(samples ? 1 : 0),
	    statementFiles().join(QChar('\x1f')),
	    dayRoot(),
	    dayName(),
	    QString::number(contest ? contest->getTaskList().size() : 0),
	    QString::number(contest ? contest->getContestantList().size() : 0),
	};

	return parts.join(QChar('|'));
}

auto PackageBuilder::innerArchiveName() const -> QString {
	if (packageKind == ContestantPackage)
		return QStringLiteral("down.zip");

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

	return QDir(dir).absoluteFilePath(Settings::exportPath() + name + QStringLiteral(".zip"));
}

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

auto PackageBuilder::collectContestantPackage(QList<Item> &items) -> bool {
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

	for (const QString &statementName : statementFiles()) {
		const QString pdf = QDir(dayRoot()).absoluteFilePath(Settings::statementPath() + statementName);

		if (QFileInfo(pdf).isFile()) {
			Item item;
			item.archivePath = prefix + statementName;
			item.sourcePath = pdf;
			items.append(item);
		} else {
			emit logMessage(tr("Statement file not found: %1").arg(QDir::toNativeSeparators(pdf)));
		}
	}

	const QList<Task *> taskList = contest->getTaskList();

	for (auto *task : taskList) {
		const QString taskName = task->getDirectoryName();
		const QString folder = prefix + Lemon::common::FileNameSafePart(taskName, QStringLiteral("task"));
		const QList<QPair<QString, QString>> files = collectDownFiles(taskName);

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

auto PackageBuilder::graderFilesForTask(const Task *task, bool quiet) -> QList<QPair<QString, QString>> {
	QList<QPair<QString, QString>> files;

	if (! task)
		return files;

	const QString taskName = task->getDirectoryName();

	if (taskName.isEmpty())
		return files;

	QStringList wanted;

	switch (task->getTaskType()) {
		case Task::Interaction:
			wanted << task->getInteractor() << task->getGrader();
			break;

		case Task::Communication:
		case Task::CommunicationExec:
			wanted << task->getGraderFilesPath();
			break;

		default:
			break;
	}

	if (task->getComparisonMode() == Task::LemonSpecialJudgeMode ||
	    task->getComparisonMode() == Task::TestlibSpecialJudgeMode)
		wanted << task->getSpecialJudge();

	const QDir graders(QDir(dayRoot()).absoluteFilePath(Settings::dataPath() +
	                                                    Settings::gradersPath(taskName)));
	QSet<QString> seen;

	for (const QString &path : wanted) {
		if (path.trimmed().isEmpty())
			continue;

		const QString name = QFileInfo(path).fileName();

		if (name.isEmpty() || seen.contains(name))
			continue;

		seen.insert(name);

		const QString diskPath = graders.absoluteFilePath(name);

		if (! QFileInfo::exists(diskPath)) {
			if (! quiet)
				emit logMessage(tr("%1 of task %2 was not found in graders/").arg(name, taskName));

			continue;
		}

		files.append({name, diskPath});
	}

	return files;
}

auto PackageBuilder::taskConfigKey(const Task *task) -> QString {
	if (! task)
		return {};

	return QStringLiteral("%1|%2|%3|%4|%5|%6")
	    .arg(int(task->getTaskType()))
	    .arg(int(task->getComparisonMode()))
	    .arg(task->getInteractor())
	    .arg(task->getGrader())
	    .arg(task->getSpecialJudge())
	    .arg(task->getGraderFilesPath().join(QChar(',')));
}

namespace {
	QString dataPathRelativeToTask(const Task *task, const QString &stored) {
		const QString taskName = task->getDirectoryName();
		const QString path = QDir::fromNativeSeparators(stored);

		if (taskName.isEmpty() || path.isEmpty())
			return {};

		const QString marker = taskName + QStringLiteral("/data/");
		const int at = path.indexOf(marker);

		if (at >= 0)
			return path.mid(at + marker.size());

		QString rest = path;
		const QString taskPrefix = taskName + QChar('/');

		if (rest.startsWith(taskPrefix))
			rest = rest.mid(taskPrefix.size());

		if (rest.startsWith(QStringLiteral("data/")))
			rest = rest.mid(5);

		if (rest.startsWith(QChar('/')) || rest.contains(QChar(':')))
			rest = QFileInfo(rest).fileName();

		return rest;
	}

	QStringList referencedDataFiles(const Task *task) {
		QSet<QString> used;

		for (auto *testCase : task->getTestCaseList()) {
			if (! testCase)
				continue;

			for (const QString &stored : testCase->getInputFiles()) {
				const QString relative = dataPathRelativeToTask(task, stored);

				if (! relative.isEmpty())
					used.insert(relative);
			}

			for (const QString &stored : testCase->getOutputFiles()) {
				const QString relative = dataPathRelativeToTask(task, stored);

				if (! relative.isEmpty())
					used.insert(relative);
			}
		}

		QStringList result(used.begin(), used.end());
		std::sort(result.begin(), result.end());
		return result;
	}
} 

auto PackageBuilder::collectTestDataFiles(const Task *task, bool quiet) -> QList<QPair<QString, QString>> {
	QList<QPair<QString, QString>> files;

	if (! task)
		return files;

	const QString taskName = task->getDirectoryName();

	const QStringList usedData = referencedDataFiles(task);

	const QString cacheKey = taskName + QChar('|') + (structure ? QChar('1') : QChar('0')) +
	                         (samples ? QChar('1') : QChar('0')) + QChar('|') + taskConfigKey(task) +
	                         QChar('|') + dayRoot() + QChar('|') + usedData.join(QChar('\n'));

	if (taskFileCache.contains(cacheKey))
		return taskFileCache.value(cacheKey);

	const QString taskRoot =
	    QDir(dayRoot()).absoluteFilePath(Settings::dataPath() + taskName + QDir::separator());

	const QDir dataDir(taskRoot + QStringLiteral("data"));

	if (! dataDir.exists()) {
		if (! quiet)
			emit logMessage(tr("No %1 folder for task %2").arg(QStringLiteral("data"), taskName));
	} else if (usedData.isEmpty() && ! quiet) {
		emit logMessage(tr("Task %1 has no test case referencing data/ files").arg(taskName));
	}

	for (const QString &relative : usedData) {
		const QString diskPath = dataDir.absoluteFilePath(relative);

		if (! QFileInfo::exists(diskPath)) {
			if (! quiet)
				emit logMessage(tr("%1 of task %2 was not found in data/").arg(relative, taskName));

			continue;
		}

		const QString archiveRelative =
		    structure ? QStringLiteral("data") + QChar('/') + relative : QFileInfo(relative).fileName();
		files.append({archiveRelative, diskPath});
	}

	if (samples) {
		const QDir downDir(taskRoot + QStringLiteral("down"));

		if (downDir.exists()) {
			QDirIterator iterator(downDir.path(), QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
			                      QDirIterator::NoIteratorFlags);

			while (iterator.hasNext()) {
				iterator.next();
				const QString name = downDir.relativeFilePath(iterator.filePath());
				files.append({QStringLiteral("down") + QChar('/') + name, iterator.filePath()});
			}
		}
	}

	for (const auto &file : graderFilesForTask(task, quiet)) {
		const QString relative =
		    structure ? QStringLiteral("graders") + QChar('/') + file.first : file.first;
		files.append({relative, file.second});
	}

	std::sort(files.begin(), files.end(),
	          [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
		          return a.first < b.first;
	          });

	if (taskFileCache.size() > 64)
		taskFileCache.clear();

	taskFileCache.insert(cacheKey, files);
	return files;
}

auto PackageBuilder::hasDuplicateTaskFiles() -> bool {
	const QString key = inputKey();

	if (duplicatesCache.contains(key))
		return duplicatesCache.value(key);

	bool duplicated = false;

	if (contest) {
		QSet<QString> seen;

		for (auto *task : contest->getTaskList()) {
			for (const auto &file : collectTestDataFiles(task, true)) {
				const QString name = structure ? file.first : QFileInfo(file.first).fileName();

				if (seen.contains(name)) {
					duplicated = true;
					break;
				}

				seen.insert(name);
			}

			if (duplicated)
				break;
		}
	}

	duplicatesCache.insert(key, duplicated);
	return duplicated;
}

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

auto PackageBuilder::collectAnswersPackage(Plan &plan) -> bool {
	if (! contest) {
		error = tr("No contest");
		return false;
	}

	const bool byRegion = contest->getRegionEnabled();
	const QString dayFolder = Lemon::common::FileNameSafePart(dayName(), QStringLiteral("day"));

	QMap<QString, QList<Contestant *>> groups;

	for (auto *contestant : contest->getContestantList())
		groups[byRegion ? contestant->getRegion().trimmed() : QString()].append(contestant);

	if (groups.isEmpty()) {
		error = tr("No contestant in current contest");
		return false;
	}

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
		const QString label =
		    region.isEmpty() ? dayFolder : Lemon::common::FileNameSafePart(region, QStringLiteral("region"));
		const QString base = dayPrefix + (region.isEmpty() ? QString() : label + QChar('/'));
		const QList<Item> groupItems = it.value();
		int count = 0;

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

	const QString key = inputKey();

	if (planCache.contains(key))
		return planCache.value(key);

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

	if (planCache.size() > 8)
		planCache.clear();

	planCache.insert(key, plan);
	return plan;
}

auto PackageBuilder::build() -> bool {
	error.clear();

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

	std::unique_ptr<ArchiveWriter> writer = makeWriter(&out, pass.toUtf8(), encryption);
	int fileCount = 0;
	int dirCount = 0;

	if (! writeItems(writer.get(), plan.items, &error, &fileCount, &dirCount))
		return false;

	for (const NestedArchive &archive : plan.archives) {
		QBuffer buffer;
		buffer.open(QIODevice::WriteOnly);
		std::unique_ptr<ArchiveWriter> inner = makeWriter(&buffer, pass.toUtf8(), encryption);
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
