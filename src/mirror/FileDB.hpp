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

#include <cassert>
#include <cstddef>
#include <afc/md5.hpp>
#include <openssl/md5.h>
#include <afc/dateutil.hpp>
#include <afc/string_util.hpp>
#include <afc/utils.h>
#include <sqlite3.h>
#include <sys/types.h>

namespace mirror
{

struct FileRecord
{
	unsigned char md5Digest[MD5_DIGEST_LENGTH];
	afc::Timestamp lastModifiedTS;
	off_t fileSize;
};

class FileDB
{
private:
	FileDB(const FileDB &) = delete;
	FileDB &operator=(const FileDB &) = delete;
	FileDB &operator=(FileDB &&) = delete;

	FileDB(const char * const dbPathInUtf8);
public:
	FileDB(FileDB &&src) : m_conn(src.m_conn), m_addFileStmt(src.m_addFileStmt),
			m_getFileStmt(src.m_getFileStmt) { src.m_conn = nullptr; }

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
		sqlite3_finalize(m_getFileStmt);
		sqlite3_finalize(m_addFileStmt);
		sqlite3_close(m_conn);

		m_conn = nullptr;
	}

	void beginTransaction();
	void commit();
	void rollback();

	void addFile(const char *fileName, std::size_t fileNameSize,
			const char *dirName, std::size_t dirNameSize, const FileRecord &data);
	void getFile(const char *fileName, const std::size_t fileNameSize,
			const char *dirName, std::size_t dirNameSize, FileRecord &dest);
private:
	sqlite3 *m_conn;
	sqlite3_stmt *m_addFileStmt;
	sqlite3_stmt *m_getFileStmt;
};

}

inline void mirror::FileDB::beginTransaction()
{
	assert(m_conn != nullptr);
	const int result = sqlite3_exec(m_conn, u8"begin transaction", nullptr, nullptr, nullptr);
	if (result != SQLITE_OK) {
		throw sqlite3_errstr(result);
	}
}

inline void mirror::FileDB::commit()
{
	assert(m_conn != nullptr);
	const int result = sqlite3_exec(m_conn, u8"commit", nullptr, nullptr, nullptr);
	if (result != SQLITE_OK) {
		throw sqlite3_errstr(result);
	}
}

inline void mirror::FileDB::rollback()
{
	assert(m_conn != nullptr);
	const int result = sqlite3_exec(m_conn, u8"rollback", nullptr, nullptr, nullptr);
	if (result != SQLITE_OK) {
		throw sqlite3_errstr(result);
	}
}

#endif // MIRROR_FILEDB_HPP_
