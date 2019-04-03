/* mirror - a tool to make mirrors of files or directories and to check consistency of the existing mirrors.
Copyright (C) 2017-2019 Dźmitry Laŭčuk

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>. */
#ifndef MIRROR_FILEDB_HPP_
#define MIRROR_FILEDB_HPP_

#include <afc/builtin.hpp>
#include <afc/dateutil.hpp>
#include <afc/logger.hpp>
#include <afc/SimpleString.hpp>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <numeric>
#include <afc/string_util.hpp>
#include <afc/utils.h>
#include <sqlite3.h>
#include <sys/types.h>
#include <unordered_map>
#include <unordered_set>

namespace mirror
{
	struct PathKey
	{
		explicit PathKey(const char * const valU8, const bool tmp = false) : hash(0), owner(!tmp)
		{
			// TODO check how memory reads/writes are issued by the compiler, optimise if needed.
			if (tmp) {
				data = valU8;
				size = 0;

				for (const char *p = valU8; *p != 0; ++p, ++size) {
					hash = (hash << 7) + *p;
				}
			} else {
				afc::U8String str(valU8);
				data = str.data();
				size = str.size();

				const char *p = valU8;
				for (std::size_t i = size; i > 0; --i) {
					hash = (hash << 7) + *p++;
				}

				str.detach();
			}
		}

		PathKey(const char * const valU8, const std::size_t n, const bool tmp = false)
				: size(n), hash(0), owner(!tmp)
		{
			// TODO check how memory reads/writes are issued by the compiler, optimise if needed.
			if (tmp) {
				data = valU8;
			} else {
				data = afc::U8String(valU8, n).detach();
			}

			const char *p = valU8;
			for (std::size_t i = n; i > 0; --i) {
				hash = (hash << 7) + *p++;
			}
		}

		PathKey(const PathKey &) = delete;
		PathKey(PathKey &&o) noexcept : data(o.data), size(o.size), hash(o.hash), owner(o.owner) { o.owner = false; }

		PathKey &operator=(const PathKey &) = delete;
		PathKey &operator=(PathKey &&o) noexcept
		{
			data = o.data;
			size = o.size;
			hash = o.hash;
			owner = o.owner;
			o.owner = false;
			return *this;
		}

		~PathKey() { if (owner) { std::free(const_cast<char *>(data)); } }

		const char *data;
		std::size_t size;
		std::size_t hash;
		bool owner;
	};

	struct PathHash
	{
		std::size_t operator()(const PathKey &val) const noexcept
		{
			return val.hash;
		}
	};

	struct PathEquals
	{
		bool operator()(const PathKey &a, const PathKey &b) const noexcept
		{
			return a.size == b.size && std::equal(a.data, a.data + a.size, b.data);
		}
	};

	enum FileType
	{
		file = 0, dir = 1
	};

	struct FileRecord
	{
		FileType type;
		unsigned char crc64[8];
		afc::Timestamp lastModifiedTS;
		off_t fileSize;
	};

	using DirFileMap = std::unordered_map<PathKey, FileRecord, PathHash, PathEquals>;
	using DirSet = std::unordered_set<PathKey, PathHash, PathEquals>;

	class FileDB
	{
	private:
		FileDB(const FileDB &) = delete;
		FileDB &operator=(const FileDB &) = delete;
		FileDB &operator=(FileDB &&) = delete;

		FileDB(const char * const dbPathInUtf8);
	public:
		FileDB(FileDB &&src) : m_conn(src.m_conn), m_addFileStmt(src.m_addFileStmt), m_getFileStmt(src.m_getFileStmt),
				m_getDirFilesStmt(src.m_getDirFilesStmt), m_getDirsStmt(src.m_getDirsStmt) { src.m_conn = nullptr; }

		~FileDB()
		{
			assert(m_conn == nullptr);
		}

		static FileDB open(const char * const fileName, const bool create = false)
		{
			return FileDB(afc::convertToUtf8(fileName, afc::systemCharset().c_str()).c_str());
		}

		void close()
		{
			// TODO handle result codes.
			sqlite3_finalize(m_getDirsStmt);
			sqlite3_finalize(m_getDirFilesStmt);
			sqlite3_finalize(m_getFileStmt);
			sqlite3_finalize(m_addFileStmt);
			sqlite3_close(m_conn);

			m_conn = nullptr;
		}

		void beginTransaction(void);
		void commit(void);
		void rollback(void);

		void addFile(const char *fileNameU8, std::size_t fileNameSize,
				const char *dirNameU8, std::size_t dirNameSize, const FileRecord &data);
		void getFile(const char *fileNameU8, std::size_t fileNameSize,
				const char *dirNameU8, std::size_t dirNameSize, FileRecord &dest);
		void getFiles(const char *dirNameU8, std::size_t dirNameSize, DirFileMap &dest);
		void getDirs(DirSet &dest);
	private:
		sqlite3 *m_conn;
		sqlite3_stmt *m_addFileStmt;
		sqlite3_stmt *m_getFileStmt;
		sqlite3_stmt *m_getDirFilesStmt;
		sqlite3_stmt *m_getDirsStmt;
	};
}

namespace afc
{
	namespace logger
	{
		template<>
		inline bool logPrint<const mirror::FileType &>(const mirror::FileType &val, std::FILE * const dest)
		{
			using afc::operator"" _s;

			assert(val == mirror::FileType::file || val == mirror::FileType::dir);

			auto text = val == mirror::FileType::file ? "File"_s : "Dir"_s;

			return afc::logger::logPrint(text, dest);
		}
	}
}

inline void mirror::FileDB::beginTransaction(void)
{
	assert(m_conn != nullptr);
	const int result = sqlite3_exec(m_conn, u8"begin transaction", nullptr, nullptr, nullptr);
	if (result != SQLITE_OK) {
		throw sqlite3_errstr(result);
	}
}

inline void mirror::FileDB::commit(void)
{
	assert(m_conn != nullptr);
	const int result = sqlite3_exec(m_conn, u8"commit", nullptr, nullptr, nullptr);
	if (result != SQLITE_OK) {
		throw sqlite3_errstr(result);
	}
}

inline void mirror::FileDB::rollback(void)
{
	assert(m_conn != nullptr);
	const int result = sqlite3_exec(m_conn, u8"rollback", nullptr, nullptr, nullptr);
	if (result != SQLITE_OK) {
		throw sqlite3_errstr(result);
	}
}

#endif // MIRROR_FILEDB_HPP_
