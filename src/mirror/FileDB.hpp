/* mirror - a tool to make mirrors of files or directories and to check consistency of the existing mirrors.
Copyright (C) 2017 Dźmitry Laŭčuk

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
#include <afc/SimpleString.hpp>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <numeric>
#include <openssl/md5.h>
#include <afc/string_util.hpp>
#include <afc/utils.h>
#include <sqlite3.h>
#include <sys/types.h>
#include <unordered_map>
#include <unordered_set>

namespace mirror
{
	struct PathHash
	{
		std::size_t operator()(const afc::U8String &val) const noexcept
		{
			return std::accumulate(val.begin(), val.end(), std::size_t(0),
					[](const std::size_t result, const char c) { return (result << 7) + c; });
		}
	};

	struct PathEquals
	{
		bool operator()(const afc::U8String &a, const afc::U8String &b) const noexcept
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
		}
	};

	struct FileRecord
	{
		unsigned char md5Digest[MD5_DIGEST_LENGTH];
		afc::Timestamp lastModifiedTS;
		off_t fileSize;
	};

	using DirFileMap = std::unordered_map<afc::U8String, FileRecord, PathHash, PathEquals>;
	using DirSet = std::unordered_set<afc::U8String, PathHash, PathEquals>;

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
