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
#include "core/testcase.h"
//
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
	// AES-256 分组加密（FIPS-197，只做加密）与 WinZip AES / AE-2 需要的密钥派生
	// ------------------------------------------------------------------
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

	/// AES-256 只用于压缩包条目：只实现加密方向，CTR 模式也不需要解密。
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

			// AES-256：8 个 32 位字，共 60 个字（15 组轮密钥）。
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

		/// 状态按列存放：state[column * 4 + row]。
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

	/// WinZip AES 的计数器：16 字节小端整数，第一块从 1 开始。
	void advanceCounter(unsigned char counter[16]) {
		for (int i = 0; i < 16; i++)
			if (++counter[i] != 0)
				break;
	}

	/// AES-CTR：WinZip AES 用「计数器块加密后异或明文」，加解密同一个函数。
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

		// 密钥不足一个分组时补 0；不能用 QByteArray::resize() 扩容，多出来的字节不保证是 0。
		for (int i = 0; i < blockSize; i++) {
			const char byte = i < shortKey.size() ? shortKey.at(i) : '\0';
			innerPad[i] = char(static_cast<unsigned char>(byte) ^ 0x36);
			outerPad[i] = char(static_cast<unsigned char>(byte) ^ 0x5c);
		}

		const QByteArray inner = QCryptographicHash::hash(innerPad + data, QCryptographicHash::Sha1);
		return QCryptographicHash::hash(outerPad + inner, QCryptographicHash::Sha1);
	}

	/// PBKDF2-HMAC-SHA1（RFC 2898）：WinZip AES 规定 1000 轮。
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

	/// 小端整数（zip 的 extra field 用）。
	QByteArray littleEndian16(quint16 value) {
		QByteArray bytes(2, '\0');
		bytes[0] = char(value & 0xFFu);
		bytes[1] = char((value >> 8) & 0xFFu);
		return bytes;
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

	/// 加密的 zip 写入器：AES-256（WinZip AES / AE-2，默认）或传统 ZipCrypto（仅兼容）。
	/// 文件名照旧，文件内容先 deflate 再加密。
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

			// 目录条目不加密，只记一个以 / 结尾的空条目
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

			// 压不小就直接存原始数据
			if (payload.isEmpty() || payload.size() >= data.size()) {
				payload = data;
				method = 0;
			}

			QByteArray body;
			QByteArray extra;
			quint32 storedCrc = crc;
			quint16 version = 20;

			if (this->method == Method::Aes256) {
				// WinZip AES / AE-2：salt + 2 字节校验值 + 密文 + 10 字节 HMAC-SHA1。
				// CRC 在 AE-2 里固定写 0（完整性由 HMAC 保证）。
				QByteArray salt(16, '\0');

				for (char &byte : salt)
					byte = char(QRandomGenerator::global()->bounded(256));

				// 66 = 32 字节 AES 密钥 + 32 字节 HMAC 密钥 + 2 字节密码校验值
				const QByteArray derived = pbkdf2Sha1(password, salt, 1000, 66);
				const QByteArray aesKey = derived.left(32);
				const QByteArray macKey = derived.mid(32, 32);
				const QByteArray verifier = derived.mid(64, 2);
				const QByteArray encrypted = aesCtrCrypt(aesKey, payload);
				const QByteArray auth = hmacSha1(macKey, encrypted).left(10);

				// 自检：同一把密钥异或回去必须还原出压缩后的明文，HMAC 也要对得上。
				// 宁可在这里报错，也不要悄悄写出一个打不开的包。
				if (aesCtrCrypt(aesKey, encrypted) != payload || ! hmacSha1(macKey, encrypted).startsWith(auth))
					selfCheckFailed = true;

				body = salt + verifier + encrypted + auth;
				// extra field 0x9901：版本 AE-2、厂商 "AE"、强度 3（AES-256）、真实压缩方式
				extra = littleEndian16(0x9901) + littleEndian16(7) + littleEndian16(2) + QByteArray("AE", 2) +
				        QByteArray(1, char(0x03)) + littleEndian16(method);
				storedCrc = 0;
				version = 51;
				method = 99; // AES
			} else {
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
				body = encrypted;

				// 自检：拿同一个密码把刚写出的 12 字节头解回来，最后一字节必须等于 CRC 高位。
				// 这里曾经出过「头与正文用错密钥流」的 bug：包能正常生成，解压时却一律报
				// 「密码错误」。就地验一下，宁可报错，也不要悄悄给出一个打不开的包。
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
				writeU16(entry.versionNeeded); // version made by
				writeU16(entry.versionNeeded); // version needed
				writeU16(entry.flags);
				writeU16(entry.method);
				writeU16(dosTime);
				writeU16(dosDate);
				writeU32(entry.crc);
				writeU32(entry.compressedSize);
				writeU32(entry.uncompressedSize);
				writeU16(quint16(entry.name.size()));
				writeU16(quint16(entry.extra.size())); // extra
				writeU16(0); // comment
				writeU16(0); // disk
				writeU16(0); // internal attributes
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
		                      quint16 flags, const QByteArray &extra, quint16 version) {
			lastOffset = quint32(out->pos());
			writeU32(0x04034b50);
			writeU16(version); // version needed
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

			/// 解密：密文 ^ 同一个密钥流 = 明文，密钥同样用明文字节推进。
			/// （自检用；写出来的包是给外面的工具解的，这里只验自己写对没有。）
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
		/// 加密自检没过（见 addFile()），finish() 会直接报错。
		bool selfCheckFailed{false};
	};

	std::unique_ptr<ArchiveWriter> makeWriter(QIODevice *device, const QByteArray &password,
	                                          PackageBuilder::EncryptionMethod method) {
		if (password.isEmpty())
			return std::make_unique<QtArchiveWriter>(device);

		// 默认用 AES-256；只有明确选了兼容模式才用老式 ZipCrypto。
		const auto writerMethod = method == PackageBuilder::ZipCryptoEncryption
		                              ? EncryptedArchiveWriter::Method::ZipCrypto
		                              : EncryptedArchiveWriter::Method::Aes256;
		return std::make_unique<EncryptedArchiveWriter>(device, password, writerMethod);
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

// 下面这些 setter 都不清缓存：缓存是按「输入签名」存的，
// 改了哪个选项，只是对应不上旧键而已，别的键仍然有效 —— 这正是来回切包类型能秒开的原因。

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

	// 换了比赛就丢掉旧结果（同一个比赛内的增删题/选手由签名里的数量覆盖）。
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

	// 样例数据必须保留结构，否则 data/ 与 down/ 里的同名文件会撞在一起。
	if (value && ! structure)
		structure = true;
}

void PackageBuilder::setStatementFiles(const QStringList &values) {
	statements.clear();

	for (const QString &value : values) {
		// 只取文件名部分，免得传进来的路径把题面放到 statement/ 外面去。
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

// 输入签名：只要这些没变，算出来的计划就一样。
// 除了包类型和各个选项，还要带上比赛日目录 / 比赛日名（套层目录名、内层包名都用它），
// 以及试题 / 选手数量，这样增删了题或选手也会自动换键。
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

	return QDir(dir).absoluteFilePath(Settings::exportPath() + name + QStringLiteral(".zip"));
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

	// 题面：放在压缩包根目录，可以是 0 个到任意多个，名字就是 statement/ 下的文件名。
	for (const QString &statementName : statementFiles()) {
		const QString pdf = QDir(dayRoot()).absoluteFilePath(Settings::statementPath() + statementName);

		if (QFileInfo(pdf).isFile()) {
			Item item;
			item.archivePath = prefix + statementName;
			item.sourcePath = pdf;
			items.append(item);
		} else {
			// 缺题面照样能打包，只是提醒一句（界面会弹窗再确认一次）。
			emit logMessage(tr("Statement file not found: %1").arg(QDir::toNativeSeparators(pdf)));
		}
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

// 一道题要带上的 graders/ 文件，与评测时实际拷进工作目录的保持一一对应：
//   交互题：交互库 + 主交互程序；通信题：通信库；使用 SPJ（Lemon / testlib 校验器）：校验器源码。
// 传统题 / 提交答案题不需要 graders/ 里的任何东西。
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

	// 用了校验器的题目，把校验器源码一并带上（编译好的可执行文件不导出）。
	if (task->getComparisonMode() == Task::LemonSpecialJudgeMode ||
	    task->getComparisonMode() == Task::TestlibSpecialJudgeMode)
		wanted << task->getSpecialJudge();

	// gradersPath() 是相对 dataPath() 的，和评测时找交互库 / 校验器的方式保持一致。
	const QDir graders(QDir(dayRoot()).absoluteFilePath(Settings::dataPath() +
	                                                    Settings::gradersPath(taskName)));
	QSet<QString> seen;

	for (const QString &path : wanted) {
		if (path.trimmed().isEmpty())
			continue;

		// 工程里存的各种写法（可能是 <题>/graders/x、也可能只是 x）都按文件名到 graders/ 下找。
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

// 与打包范围有关的题目配置：题型、判题方式、交互库 / 通信库 / 校验器的路径。
// 改了这些，题目要带的文件就变了，缓存键也得跟着变。
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
	/// .cdf 里存的是相对 `problem/` 的测试数据路径（形如 `<题>/data/sub/1.in`）；
	/// 取出 `problem/<题>/data/` 之后的部分，就是相对 data/ 的路径（`sub/1.in`）。
	QString dataPathRelativeToTask(const Task *task, const QString &stored) {
		const QString taskName = task->getDirectoryName();
		const QString path = QDir::fromNativeSeparators(stored);

		if (taskName.isEmpty() || path.isEmpty())
			return {};

		const QString marker = taskName + QStringLiteral("/data/");
		const int at = path.indexOf(marker);

		if (at >= 0)
			return path.mid(at + marker.size());

		// 老工程可能只存了 `<题>/<文件>`，甚至只有文件名；都按 data/ 下的文件处理。
		QString rest = path;
		const QString taskPrefix = taskName + QChar('/');

		if (rest.startsWith(taskPrefix))
			rest = rest.mid(taskPrefix.size());

		if (rest.startsWith(QStringLiteral("data/")))
			rest = rest.mid(5);

		// 兜底：绝对路径只取文件名。
		if (rest.startsWith(QChar('/')) || rest.contains(QChar(':')))
			rest = QFileInfo(rest).fileName();

		return rest;
	}

	/// 一道题所有测试点引用到的数据文件（相对 `<题>/data/`，去重并按字典序）。
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
} // namespace

// 测试数据包：一道题要打包的文件（相对题目录的路径, 磁盘路径）。
// data/ 只带测试点真正引用到的输入 / 输出文件（这些路径就记在 .cdf 里，出题人丢在 data/
// 里但没挂到测试点上的中间产物不导出）；graders/ 只带评测真正用得上的那几个文件
// （见 graderFilesForTask）；down/（样例数据）只有勾了才带上；gen/、tests/ 这类中间产物不导出。
auto PackageBuilder::collectTestDataFiles(const Task *task, bool quiet) -> QList<QPair<QString, QString>> {
	QList<QPair<QString, QString>> files;

	if (! task)
		return files;

	const QString taskName = task->getDirectoryName();

	// data/ 下真正被测试点引用的文件（相对 <题>/data/）。
	const QStringList usedData = referencedDataFiles(task);

	// 缓存键带上「保留结构 / 样例数据」、题目配置、比赛日与被引用的数据文件：
	// 重名检查和收集计划共用同一次扫描。
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

		// 保留结构时照搬 data/ 下的相对路径（子目录也带上）；不保留时只留文件名
		// （撞名会被跳过并记日志）。
		const QString archiveRelative =
		    structure ? QStringLiteral("data") + QChar('/') + relative : QFileInfo(relative).fileName();
		files.append({archiveRelative, diskPath});
	}

	if (samples) {
		// 样例数据只收 down/ 本层的文件：down/ 里的子目录（例如按测试点分的子目录）
		// 是出题人自己的中间产物，不往包里带。
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

	// graders/：只要评测用得上的那几个文件（交互库 / 通信库 / 校验器源码）。
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

// 各题的文件是否重名：重名的话「各题单独一个目录」就不能关。
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

	// 同样的输入（包类型 + 选项 + 比赛日）直接给上次的结果，
	// 这样在选项卡里来回切换、把选项改回原样都不会再扫一遍磁盘。
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

	// 只留最近几份，别无限涨。
	if (planCache.size() > 8)
		planCache.clear();

	planCache.insert(key, plan);
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

	std::unique_ptr<ArchiveWriter> writer = makeWriter(&out, pass.toUtf8(), encryption);
	int fileCount = 0;
	int dirCount = 0;

	if (! writeItems(writer.get(), plan.items, &error, &fileCount, &dirCount))
		return false;

	// 内层压缩包：每个先在内存里打一份（同样受密码保护），再作为一项放进外层。
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
